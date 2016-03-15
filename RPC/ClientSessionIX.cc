/* Copyright (c) 2012-2014 Stanford University
 * Copyright (c) 2014-2015 Diego Ongaro
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR(S) DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <assert.h>
#include <string.h>
#include <unistd.h>

#include "Core/Debug.h"
#include "Core/StringUtil.h"
#include "Protocol/Common.h"

#include "RPC/ClientSessionIX.h"

namespace LogCabin {
namespace RPC {


////////// ClientSession::HandlerForIx //////////

ClientSession::HandlerForIX::HandlerForIX(
    ClientSession& session)
    : session(session)
{
}

void
ClientSession::HandlerForIX::handleReceivedMessage(
    ixev_ctx* ctx,
    MessageId messageId,
    Core::Buffer message)
{

    if (messageId == Protocol::Common::PING_MESSAGE_ID) {
        if (session.numActiveRPCs > 0 && session.activePing) {
            // The server has shown that it is alive for now.
            // Let's get suspicious again in another PING_TIMEOUT_NS.
            session.activePing = false;
            //session.timer.schedule(session.PING_TIMEOUT_NS);
        } else {
            VERBOSE("Received an unexpected ping response. This can happen "
                    "for a number of reasons and is no cause for alarm. For "
                    "example, this happens if a ping request was sent out, "
                    "then all RPCs completed before the ping response "
                    "arrived.");
        }
        return;
    }

    auto it = session.responses.find(messageId);
    if (it == session.responses.end()) {
        VERBOSE("Received an unexpected response with message ID %lu. "
                "This can happen for a number of reasons and is no cause "
                "for alarm. For example, this happens if the RPC was "
                "cancelled before its response arrived.",
                messageId);
        return;
    }
    Response& response = *it->second;
    if (response.status == Response::HAS_REPLY) {
        WARNING("Received a second response from the server for "
                "message ID %lu. This indicates that either the client or "
                "server is assigning message IDs incorrectly, or "
                "the server is misbehaving. Dropped this response.",
                messageId);
        return;
    }

    // Book-keeping for timeouts
    --session.numActiveRPCs;

    // Timer has to be implemented look at linux implementation
    // if (session.numActiveRPCs == 0)
    //     session.timer.deschedule();
    // else
    //     session.timer.schedule(session.PING_TIMEOUT_NS);

    // Fill in the response
    response.status = Response::HAS_REPLY;
    response.reply = std::move(message);
    response.ready.notify_all();

}

void
ClientSession::HandlerForIX::handleDisconnect()
{
    VERBOSE("Disconnected from server %s",
            session.address.toString().c_str());
    std::lock_guard<std::mutex> mutexGuard(session.mutex);
    if (session.errorMessage.empty()) {
        // Fail all current and future RPCs.
        session.errorMessage = ("Disconnected from server " +
                                session.address.toString());
        // Notify any waiting RPCs.
        for (auto it = session.responses.begin();
             it != session.responses.end();
             ++it) {
            Response* response = it->second;
            response->ready.notify_all();
        }
    }
}

////////// ClientSession::Response //////////

ClientSession::Response::Response()
    : status(Response::WAITING)
    , reply()
    , hasWaiter(false)
    , ready()
{
}


////////// ClientSession //////////

ClientSession::ClientSession(const Address& address,
                             uint32_t maxMessageLength,
                             TimePoint timeout,
                             const Core::Config& config)
    : self() // makeSession will fill this in shortly
    , PING_TIMEOUT_NS(config.read<uint64_t>(
        "tcpHeartbeatTimeoutMilliseconds", 500) * 1000 * 1000)
    , address(address)
    , ixHandler(*this)
    , mutex()
    , nextMessageId(0)
    , responses()
    , errorMessage()
    , numActiveRPCs(0)
    , activePing(false)
    , ctx()
{

    ctx = IX::initClientConnection(&ixHandler, address.getAddressAt(0),
                                   address.getPortAt(0));
    if (!ctx) {
        errorMessage = "failed to connect";
        return;
    }
}

std::shared_ptr<ClientSession>
ClientSession::makeSession(const Address& address,
                           uint32_t maxMessageLength,
                           TimePoint timeout,
                           const Core::Config& config)
{
    std::shared_ptr<ClientSession> session(
        new ClientSession(address,
                          maxMessageLength,
                          timeout,
                          config));
    session->self = session;
    return session;
}

std::shared_ptr<ClientSession>
ClientSession::makeErrorSession(const std::string& errorMessage)
{
    Core::Config config;
    std::shared_ptr<ClientSession> session(
        new ClientSession(Address(),
                          0,
                          TimePoint::min(),
                          config));
    session->self = session;
    session->errorMessage = errorMessage;
    return session;
}



ClientSession::~ClientSession()
{
//    IX::close(ctx);
    ixHandler.handleDisconnect();
    for (auto it = responses.begin(); it != responses.end(); ++it)
        delete it->second;
}

OpaqueClientRPC
ClientSession::sendRequest(Core::Buffer request)
{
        IX::MessageId messageId;
    {

        std::lock_guard<std::mutex> mutexGuard(mutex);
        messageId = nextMessageId;
        ++nextMessageId;
        responses[messageId] = new Response();

        ++numActiveRPCs;
        if (numActiveRPCs == 1) {
            // activePing's value was undefined while numActiveRPCs = 0
            activePing = false;
            // timer events for ping messages to be implemented
            //timer.schedule(PING_TIMEOUT_NS);
        }
    }

    if (ctx) {
        IX::sendMessage(ctx, messageId, std::move(request));
    }

    OpaqueClientRPC rpc;
    rpc.session = self.lock();
    rpc.responseToken = messageId;
    return rpc;
}

std::string
ClientSession::getErrorMessage() const
{
    std::lock_guard<std::mutex> mutexGuard(mutex);
    return errorMessage;
}

std::string
ClientSession::toString() const
{
    std::string error = getErrorMessage();
    if (error.empty()) {
        return "Active session to " + address.toString();
    } else {
        // error will already include the server's address.
        return "Closed session: " + error;
    }
}

////////// ClientSession private methods //////////

void
ClientSession::cancel(OpaqueClientRPC& rpc)
{
    // The RPC may be holding the last reference to this session. This
    // temporary reference makes sure this object isn't destroyed until after
    // we return from this method. It must be the first line in this method.
    std::shared_ptr<ClientSession> selfGuard(self.lock());

    // There are two ways to cancel an RPC:
    // 1. If there's some thread currently blocked in wait(), this method marks
    //    the Response's status as CANCELED, and wait() will delete it later.
    // 2. If there's no thread currently blocked in wait(), the Response is
    //    deleted entirely.
    std::lock_guard<std::mutex> mutexGuard(mutex);
    auto it = responses.find(rpc.responseToken);
    if (it == responses.end())
        return;
    Response* response = it->second;
    if (response->hasWaiter) {
        response->status = Response::CANCELED;
        response->ready.notify_all();
    } else {
        delete response;
        responses.erase(it);
    }

    --numActiveRPCs;
    // Even if numActiveRPCs == 0, it's simpler here to just let the timer wake
    // up an extra time and clean up. Otherwise, we'd need to grab an
    // Event::Loop::Lock prior to the mutex to call deschedule() without
    // inducing deadlock.
}

void
ClientSession::update(OpaqueClientRPC& rpc)
{
    // The RPC may be holding the last reference to this session. This
    // temporary reference makes sure this object isn't destroyed until after
    // we return from this method. It must be the first line in this method.
    std::shared_ptr<ClientSession> selfGuard(self.lock());

    std::lock_guard<std::mutex> mutexGuard(mutex);
    auto it = responses.find(rpc.responseToken);
    if (it == responses.end()) {
        // RPC was cancelled, fields set already
        assert(rpc.status == OpaqueClientRPC::Status::CANCELED);
        return;
    }
    Response* response = it->second;
    if (response->status == Response::HAS_REPLY) {
        rpc.reply = std::move(response->reply);
        rpc.status = OpaqueClientRPC::Status::OK;
    } else if (!errorMessage.empty()) {
        rpc.errorMessage = errorMessage;
        rpc.status = OpaqueClientRPC::Status::ERROR;
    } else {
        // If the RPC was canceled, then it'd be marked ready and update()
        // wouldn't be called again.
        assert(response->status != Response::CANCELED);
        return; // not ready
    }
    rpc.session.reset();

    delete response;
    responses.erase(it);
}

void
ClientSession::wait(const OpaqueClientRPC& rpc, TimePoint timeout)
{
    // The RPC may be holding the last reference to this session. This
    // temporary reference makes sure this object isn't destroyed until after
    // we return from this method. It must be the first line in this method.
    std::shared_ptr<ClientSession> selfGuard(self.lock());

    std::unique_lock<std::mutex> mutexGuard(mutex);
    while (true) {
        auto it = responses.find(rpc.responseToken);
        if (it == responses.end())
            return; // RPC was cancelled or already updated
        Response* response = it->second;
        if (response->status == Response::HAS_REPLY) {
            return; // RPC has completed
        } else if (response->status == Response::CANCELED) {
            // RPC was cancelled, finish cleaning up
            delete response;
            responses.erase(it);
            return;
        } else if (!errorMessage.empty()) {
            return; // session has error
        } else if (timeout < Clock::now()) {
            return; // timeout
        }
        response->hasWaiter = true;
        response->ready.wait_until(mutexGuard, timeout);
        response->hasWaiter = false;
    }
}

} // namespace LogCabin::RPC
} // namespace LogCabin
