/* Copyright (c) 2011-2014 Stanford University
 * Copyright (c) 2015 Diego Ongaro
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

#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <mutex>

#include "Protocol/Common.h"
#include "Core/Buffer.h"

extern "C" {
#include "libix/mempool.h"
#include "libix/ixev.h"
}


#ifndef LOGCABIN_RPC_INTERFACEIX_H
#define LOGCABIN_RPC_INTERFACEIX_H

namespace LogCabin {
namespace RPC {
namespace IX {

typedef uint64_t MessageId;

// Taken from MessageSocket as well
// Interface to handler
class Handler {
    public:
    virtual void handleReceivedMessage(
        ixev_ctx* ctx, MessageId messageId, Core::Buffer contents) = 0;
    virtual ~Handler() {}
    virtual void handleDisconnect() = 0;
};



void setServerHandler(Handler* handler);
void initializeIx();
void initializeThread();
void waitForever();
void sendMessage(ixev_ctx* ctx, MessageId messageId,
                 Core::Buffer contents);
ixev_ctx* initClientConnection(Handler* handler,
                          std::string address, std::string port);

// not implemented
void close(struct ixev_ctx *ctx);

} // namespace LogCabin::RPC::IX
} // namespace LogCabin::RPC
} // namespace LogCabin

#endif /* LOGCABIN_RPC_INTERFACEIX_H */
