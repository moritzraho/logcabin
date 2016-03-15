#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "Core/Debug.h"
#include "Core/ThreadId.h"
#include "Core/Mutex.h"
#include "Core/ConditionVariable.h"

#include "RPC/InterfaceIX.h"

#include <deque>
#include <thread>
#include <mutex>

extern "C" {
#include "libix/ixev_timer.h"
#include "ix/syscall.h"
#include "net/ip.h"
}

namespace LogCabin {
namespace RPC {
namespace IX {

// TODO: put IX max message length
static const uint64_t MAX_MESSAGE_LENGTH = 128 * 8196;
//:: LogCabin::Protocol::Common::MAX_MESSAGE_LENGTH;
enum conn_type { SERVER = 0, CLIENT = 1, FIRST_REQUEST = 2 };

// Taken from LogCabin's MessageSocket
// Defines the header of a packet
struct Header {
    void fromBigEndian();
    void toBigEndian();
    uint16_t fixed;
    uint16_t version;
    uint32_t payloadLength;
    uint64_t messageId;
}__attribute__((packed));

// this defines a connection
struct ix_conn {

    struct ixev_ctx ctx;

    size_t bytes_so_far;
    Header header;
    Core::Buffer message;

    struct ip_tuple id;
    conn_type type;
    // Handler for client, redefined in ClientSession
    // Each ClientSession has its Handler instance
    // There is one client connection per ClientSession
    // Is NULL if server side connection
    Handler* client_handler;
};

// internal functions: should only be accessed by the main thread
static struct ixev_ctx* accept_cb(struct ip_tuple *id);
static void release_cb(struct ixev_ctx *ctx);
static void dialed_cb(struct ixev_ctx *ctx, long ret);

static void readable(struct ixev_ctx *ctx, unsigned int reason);
static void writable(struct ixev_ctx *ctx, unsigned int reason);
static void timer_handler (void* arg);

// helpers
static int _parse_ip_addr(const char *str, uint32_t *addr);
static void _fill_message(ix_conn *conn, MessageId messageId,
                          Core::Buffer contents);


// Handler for server, redefined in OpaqueServer
// This should be unique as their is only one
// OpaqueServer instance
static Handler* SERVER_HANDLER = nullptr;

static struct mempool_datastore ix_conn_datastore;
static struct mempool ix_conn_pool;

static struct ixev_timer timer_struct;

static struct timeval tv = {.tv_sec = 0, .tv_usec = 8L};

// container for queue elements.
// could probably be done in an easier manner.
// But do not replace with a queue of ix_conn*,
// because only the main thread should modify ix_conn.
// all this trouble because core::buffer cannot be copied
class queue_elem {

  public:
    queue_elem()
        : ctx(nullptr),
          messageId(0),
          message()
        {}

    queue_elem(queue_elem&& other)
        : ctx(other.ctx),
          messageId(other.messageId),
          message(std::move(other.message))
        {}

    // this one is for the pointer warning on ctx
    queue_elem(queue_elem& other)
        : ctx(other.ctx),
          messageId(other.messageId),
          message(std::move(other.message))
        {}

    queue_elem(ixev_ctx* ctx, MessageId messageId,
               Core::Buffer message)
        : ctx(ctx)
        , messageId(messageId)
        , message(std::move(message))
        {}

    queue_elem& operator=(queue_elem&& other)
        {
            ctx = other.ctx;
            messageId = other.messageId;
            message = std::move(other.message);
            return *this;
        }
    // this one is for the pointer warning on ctx
    queue_elem& operator=(queue_elem& other)
        {
            ctx = other.ctx;
            messageId = other.messageId;
            message = std::move(other.message);
            return *this;
        }

    ixev_ctx* ctx;
    MessageId messageId;
    Core::Buffer message;
};

static Core::Mutex send_queue_mutex;
static std::deque<queue_elem> send_queue;

static struct ixev_conn_ops ix_conn_ops = {
    .accept     = &accept_cb,
    .release    = &release_cb,
    .dialed     = &dialed_cb,
};


// This is used to dispatch the server handler
// on receiving a message.
// LogCabin provides a dispatch at the service level
// however we try to keep the main thread only inside the interface

// not a lot of threads are necessary if we dispatch at service
// check Server::registerService to enable/disable dispatch service
static const int THREADS = 3;
std::mutex work_queue_mutex;
Core::ConditionVariable worker_condition;
static std::deque<queue_elem> work_queue;
static void thread_handler_main();


static void thread_handler_main() {
    while(true) {
        queue_elem e;
        {
            std::unique_lock<std::mutex> lockGuard(work_queue_mutex);
            while(work_queue.empty() or !SERVER_HANDLER)
                worker_condition.wait(lockGuard);
            e = std::move(work_queue.front());
            work_queue.pop_front();
        }

        SERVER_HANDLER->handleReceivedMessage(e.ctx, e.messageId,
                                              std::move(e.message));
    }
}

// Interface specific functions, can be accessed from outside safely
void
setServerHandler(Handler* handler) {
    if (SERVER_HANDLER) {
        ERROR("Server handler as already been set");
        return;
    }

    SERVER_HANDLER = handler;
}

void
initializeIx() {

    int ret;

    if (Core::ThreadId::getId() != 1) {
        ERROR("Only main thread can access this function");
        return;
    }

    // change size here as needed
    // for the moment this affects only server connections
    int ix_conn_pool_entries = 12 * MEMPOOL_DEFAULT_CHUNKSIZE;

    ret = ixev_init(&ix_conn_ops);
    if (ret) {
        ERROR("failed to initialize ixev\n");
        return;
    }

    ret = mempool_create_datastore(&ix_conn_datastore,
                                   ix_conn_pool_entries,
                                   MAX_MESSAGE_LENGTH,
                                   0, MEMPOOL_DEFAULT_CHUNKSIZE,
                                   "ix_conn");
    if (ret) {
        ERROR("unable to create mempool\n");
        return;
    }

    // to use background threads
    sys_spawnmode(false);
}

void
initializeThread() {

    int ret;

    if (Core::ThreadId::getId() != 1) {
        ERROR("Only main thread can access this function");
        return;
    }

    ret = ixev_init_thread();
    if (ret) {
        ERROR("unable to init ixev thread\n");
        return;//
    }

    ret = mempool_create(&ix_conn_pool, &ix_conn_datastore);
    if (ret) {
        ERROR("unable to create mempool\n");
        return;//
    }

}

// This should be called once
void
waitForever() {

    if (Core::ThreadId::getId() != 1) {
        ERROR("Only main thread can access this function");
        return;
    }

    // hack: timer that allows to wake up main thread
    ixev_timer_init(&timer_struct, &timer_handler, NULL);
    ixev_timer_add(&timer_struct, tv);

    // need to do this dynamically and join threads when needed
    for(int i=0; i < THREADS; i++) {
        std::thread t(&thread_handler_main);
        t.detach();
    }

    while (1) {
        ixev_wait();

    }
    return;

}


void
sendMessage(ixev_ctx* ctx, MessageId messageId, Core::Buffer contents)
{
    { // Place the message on the outbound queue.
        std::lock_guard<Core::Mutex> lock(send_queue_mutex);
        send_queue.emplace_back(ctx, messageId, std::move(contents));
    }
}

ixev_ctx*
initClientConnection(Handler* handler,
                     std::string address, std::string port)
{

    // Should we allocate client connections on the pool?
    ix_conn* client_conn =
        static_cast<ix_conn*>(malloc(sizeof(struct ix_conn)));

    if (!client_conn){
        ERROR("could not allocate client connection");
        return NULL;
    }

    if (_parse_ip_addr(address.c_str(), &client_conn->id.dst_ip)) {
        //ERROR("Bad IP address '%s'", address.c_str());
        free(client_conn);
        return NULL;
    }

    client_conn->id.dst_port = (uint16_t) atoi(port.c_str());
    client_conn->type = FIRST_REQUEST;
    client_conn->client_handler = handler;

    ixev_ctx_init(&client_conn->ctx);

    return &client_conn->ctx;
}

void
close(struct ixev_ctx *ctx)
{
    // TODO need a queue to let only main thread close connections
    // this queue can be polled with the timer
    ERROR("calling close -- NOT IMPLEMENTED");
    // TODO: integrate handler(s) -> handleDisconnect()
}


// Internal functions: using the actual calls to ixev,
// only the main thread should access those

void
timer_handler (void* arg) {

    // sends one message at the time because we want to be back on the
    // event loop in case ixev_send didn't send all the message in once

    queue_elem e;
    {
        // Here the main thread can block, this is against the Ix paradigm.
        // However, it is unlikely to block for a long time.
        std::lock_guard<Core::Mutex> lock(send_queue_mutex);
        if (send_queue.empty()) {
            //repeat timer
            ixev_timer_add(&timer_struct, tv);
            return;
        }
        e = std::move(send_queue.front());
        send_queue.pop_front();
    }

    struct ix_conn *conn =
        container_of(e.ctx, struct ix_conn, ctx);


    _fill_message(conn, e.messageId,
                  std::move(e.message));

    conn->bytes_so_far = 0;

    if(conn->type == FIRST_REQUEST) {
        conn->type = CLIENT;
        ixev_dial(&conn->ctx, &conn->id);
    }
    else
        writable(&conn->ctx, IXEVOUT);

    ixev_timer_add(&timer_struct, tv);
    return;

}

struct ixev_ctx*
accept_cb(struct ip_tuple *id)
{

    struct ix_conn* conn = static_cast<ix_conn*> (mempool_alloc(&ix_conn_pool));

    // not used yet
    conn->id = *id;

    conn->bytes_so_far = 0;
    ixev_ctx_init(&conn->ctx);


    conn->type = SERVER;
    conn->client_handler = NULL;

    ixev_set_handler(&conn->ctx, IXEVIN, &readable);

    return &conn->ctx;
}

void
release_cb(struct ixev_ctx *ctx)
{
    struct ix_conn *conn =
        container_of(ctx, struct ix_conn, ctx);

    if (!conn)
        return;

    // Clear buffer
    // this may segfault -- not tested deeply
    if (conn->message.getData())
         conn->message.reset();


    // clear both queues
    {
        std::lock_guard<Core::Mutex> lock(send_queue_mutex);
        // we need to erase all pending message from the closed context
        auto it = send_queue.begin();
        while(it != send_queue.end()) {
            if (it->ctx == ctx) {
                it->message.reset();
                // erasing gives next valid iterator
                it = send_queue.erase(it);
            }
            else
                it ++;
        }
    }

    if (THREADS) {
        std::lock_guard<std::mutex> lock(work_queue_mutex);
        // we need to erase all pending message from the closed context
        auto it = work_queue.begin();
        while(it != work_queue.end()) {
            if (it->ctx == ctx) {
                it->message.reset();
                it = work_queue.erase(it);
            }
            else
                it ++;
        }
    }


    // does this actually make sense?
    // -> should we allocate client connections in the pool as well?
    if (conn->type != SERVER)
        free(conn);
    else
        mempool_free(&ix_conn_pool, conn);

}

void dialed_cb(struct ixev_ctx *ctx, long ret)
{
    if (ret) {
        ERROR("Could not connect, err = %ld\n", ret);
        return;
    }

    struct ix_conn *conn =
        container_of(ctx, struct ix_conn, ctx);
    writable(&conn->ctx, IXEVOUT);
}

void
readable(struct ixev_ctx *ctx, unsigned int reason)
{
    struct ix_conn *conn = container_of(ctx, struct ix_conn, ctx);
    ssize_t bytesRead;
    // we first read the header and then the data (2 calls, might be optimized).
    // but we need to know the payloadLength
    if (conn->bytes_so_far < sizeof(Header)) {
        bytesRead = ixev_recv(ctx, reinterpret_cast<char*>(&conn->header) +
                              conn->bytes_so_far, sizeof(Header) -
                              conn->bytes_so_far);

        if (bytesRead <= 0) {
            if (bytesRead != -EAGAIN){
                ixev_close(ctx);
            }
            return;
        }

        conn->bytes_so_far += size_t(bytesRead);
        if (conn->bytes_so_far < sizeof(Header)){
            // come back to finish reading the header.
            return;
        }

        // Transition to receiving data
        conn->header.fromBigEndian();

        if (conn->header.fixed != 0xdaf4) {
            NOTICE("Disconnecting since message doesn't start with magic "
                    "0xdaf4 (first two bytes are 0x%02x)",
                    conn->header.fixed);
            ixev_close(ctx);
            return;
        }
        if (conn->header.version != 1) {
            NOTICE("Disconnecting since message uses version %u, but "
                    "this code only understands version 1",
                    conn->header.version);
            ixev_close(ctx);
            return;
        }
        if (conn->header.payloadLength > MAX_MESSAGE_LENGTH) {
            NOTICE("Disconnecting since message is too long to receive "
                    "(message is %u bytes, limit is %lu bytes)",
                    conn->header.payloadLength, MAX_MESSAGE_LENGTH);
            ixev_close(ctx);
            return;
        }

        conn->message.setData(new char[conn->header.payloadLength],
                              conn->header.payloadLength,
                              Core::Buffer::deleteArrayFn<char>);
    }

    // Now we receive the data
    if (conn->bytes_so_far < (sizeof(Header) + conn->header.payloadLength)
        && conn->bytes_so_far >= sizeof(Header)) {

        size_t payloadBytesRead = conn->bytes_so_far - sizeof(Header);
        ssize_t bytesRead = ixev_recv(ctx,
                                      static_cast<char*>
                                      (conn->message.getData()) +
                                      payloadBytesRead,
                                      conn->header.payloadLength -
                                      payloadBytesRead);

        if (bytesRead <= 0) {
            if (bytesRead != -EAGAIN) {
                ixev_close(ctx);
            }
            return;
        }

        conn->bytes_so_far += size_t(bytesRead);
        if (conn->bytes_so_far < (sizeof(Header) +
                                  conn->header.payloadLength)) {
            return;
        }
        // received response to a request
        if (conn->type && conn->client_handler) {
            // client handleReceivedMessage way shorter to handle
            // not dispatched
            conn->client_handler->handleReceivedMessage(
                ctx, conn->header.messageId, std::move(conn->message));

        }
        else if (SERVER_HANDLER) {
            if (THREADS) {
                MessageId mId = conn->header.messageId;
                std::lock_guard<std::mutex> lockGuard(work_queue_mutex);
                {
                    work_queue.emplace_back(ctx, mId,
                                            std::move(conn->message));
                    worker_condition.notify_one();
                }
            }
            else {
                SERVER_HANDLER->handleReceivedMessage(ctx, conn->header.messageId,
                                                      std::move(conn->message));
            }
        }
        else {
            ERROR("Handler for receiving message not set");
            ixev_close(ctx);
        }

        // not really needed but doesn't hurt
        conn->bytes_so_far = 0;
    }

    return;
}

void
writable(struct ixev_ctx *ctx, unsigned int reason)
{

    struct ix_conn *conn = container_of(ctx, struct ix_conn, ctx);

    ssize_t bytesSend = 0;
    size_t total_bytes = sizeof(Header) + conn->message.getLength();

    if(conn->bytes_so_far == 0)
        conn->header.toBigEndian();

    // Let's try one shot: drawback need to copy mem arround.
    // Maybe faster to use two send calls.
    if(conn->bytes_so_far < sizeof(Header)) {

        char msg[total_bytes];
        memcpy(&msg, &conn->header, sizeof(Header));
        memcpy(&msg[sizeof(Header)], conn->message.getData(),
                    conn->message.getLength());

        bytesSend = ixev_send(ctx, &msg[conn->bytes_so_far],
                              total_bytes - conn->bytes_so_far);
    }
    else if(conn->bytes_so_far < total_bytes) {
        // could not send in one time
        bytesSend = ixev_send(ctx, static_cast<char*>(conn->message.getData()) +
                              conn->bytes_so_far - sizeof(Header),
                              total_bytes - conn->bytes_so_far);

    }

    if (bytesSend <= 0) {
        if (bytesSend != -EAGAIN){
            ixev_close(ctx);
        }
        return;
    }

    conn->bytes_so_far += (size_t) bytesSend;
    if(conn->bytes_so_far >= total_bytes) {

        conn->bytes_so_far = 0;
        ixev_set_handler(ctx, IXEVIN, &readable);
    }
    else {
        // otherwise come back here on IXEVOUT
        // This is not ok if the timer handle writes more than one message
        ixev_set_handler(ctx, IXEVOUT, &writable);
    }
}

// HEADER functions

void
Header::fromBigEndian()
{
    fixed = be16toh(fixed);
    version = be16toh(version);
    payloadLength = be32toh(payloadLength);
    messageId = be64toh(messageId);
}

void
Header::toBigEndian()
{
    fixed = htobe16(fixed);
    version = htobe16(version);
    payloadLength = htobe32(payloadLength);
    messageId = htobe64(messageId);
}

// HELPERS

static int _parse_ip_addr(const char *str, uint32_t *addr)
{
    unsigned char a, b, c, d;

    if (sscanf(str, "%hhu.%hhu.%hhu.%hhu", &a, &b, &c, &d) != 4)
        return -EINVAL;

    *addr = MAKE_IP_ADDR(a, b, c, d);
    return 0;
}

static void _fill_message(ix_conn *conn, MessageId messageId,
                          Core::Buffer contents)
{

    // Check the message length.
    if (contents.getLength() > MAX_MESSAGE_LENGTH) {
        PANIC("Message of length %lu bytes is too long to send "
              "(limit is %lu bytes)",
              contents.getLength(), MAX_MESSAGE_LENGTH);
    }

    conn->header.fixed = 0xdaf4;
    conn->header.version = 1;
    conn->header.payloadLength = uint32_t(contents.getLength());
    conn->header.messageId = messageId;
    //conn->header.toBigEndian(); // done in writable
    conn->message = std::move(contents);
}


} // namespace LogCabin::RPC::IX
} // namespace LogCabin::RPC
} // namespace LogCabin
