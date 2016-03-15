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
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "Core/Debug.h"
#include "Event/Loop.h"
#include "Protocol/Common.h"
#include "RPC/Address.h"
#include "RPC/OpaqueServerIX.h"
#include "RPC/OpaqueServerRPCIX.h"

#include <pthread.h>
#include "Core/ThreadId.h"
#include "RPC/InterfaceIX.h"

namespace LogCabin {
namespace RPC {

// OPAQUE SERVER
std::string
OpaqueServer::bind()
{
    IX::initializeIx();
    IX::initializeThread();
    return "";
}


void
OpaqueServer::set_and_wait()
{
    // dynamic allocation maybe not best
    IX::setServerHandler(new HandlerForIX(this));
    IX::waitForever();
}


OpaqueServer::OpaqueServer(Handler& handler,
                           uint32_t maxMessageLength)
    : rpcHandler(handler)
    , maxMessageLength(maxMessageLength)
{
}


OpaqueServer::~OpaqueServer()
{
    // some cleanup needed ??
}

//////////////////// HANDLER FOR IX ////////////////

OpaqueServer::HandlerForIX::HandlerForIX(OpaqueServer* server)
    : server(server)
{
}

void
OpaqueServer::HandlerForIX::handleReceivedMessage(
        ixev_ctx* ctx,
        MessageId messageId,
        Core::Buffer message)
{

    // ping and version_message_id not implemented yet
    switch (messageId) {
        case Protocol::Common::PING_MESSAGE_ID: {
            NOTICE("Responding to ping");

            //Main thread  cannot call this ok if use IX::THREADS > 0
            //IX::sendMessage(ctx, messageId, Core::Buffer());
            break;
        }
        case Protocol::Common::VERSION_MESSAGE_ID: {
               NOTICE("Responding to version request "
                    "(this server supports max version %u)",
                    MessageSocket::MAX_VERSION_SUPPORTED);
               // IX::sendMessage(
                //     ctx, messageId,
                //     Core::Buffer(response, sizeof(*response),
                //                  Core::Buffer::deleteObjectFn<Response*>)
                //     );
            break;
        }
        default: { // normal RPC request
            OpaqueServerRPC rpc(ctx, messageId, std::move(message));
            server->rpcHandler.handleRPC(std::move(rpc));
        }
    }
}

void
OpaqueServer::HandlerForIX::handleDisconnect()
{}


} // namespace LogCabin::RPC
} // namespace LogCabin
