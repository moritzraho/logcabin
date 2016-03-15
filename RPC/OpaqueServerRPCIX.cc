/* Copyright (c) 2012 Stanford University
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

#include "RPC/OpaqueServerRPCIX.h"
#include "RPC/InterfaceIX.h"

namespace LogCabin {
namespace RPC {

OpaqueServerRPC::OpaqueServerRPC()
    : request()
    , response()
    , ctx(nullptr)
    , ctxMutex()
    , messageId(~0UL)
    , responseTarget(NULL)
{
}


OpaqueServerRPC::OpaqueServerRPC(
    ixev_ctx* ctx,
    IX::MessageId messageId,
    Core::Buffer request)
  : request(std::move(request))
  , response()
  , ctx(ctx)
  , ctxMutex()
  , messageId(messageId)
  , responseTarget(NULL)
{
}

OpaqueServerRPC::OpaqueServerRPC(OpaqueServerRPC&& other)
    : request(std::move(other.request))
    , response(std::move(other.response))
    , ctx(std::move(other.ctx))
    , ctxMutex()
    , messageId(std::move(other.messageId))
    , responseTarget(std::move(other.responseTarget))
{
}

OpaqueServerRPC::~OpaqueServerRPC()
{
}

OpaqueServerRPC&
OpaqueServerRPC::operator=(OpaqueServerRPC&& other)
{
    request = std::move(other.request);
    response = std::move(other.response);
    ctx = std::move(other.ctx);
    messageId = std::move(other.messageId);
    responseTarget = std::move(other.responseTarget);
    return *this;
}

void
OpaqueServerRPC::closeSession()
{
    //TODO

    //IX::close(ctx);

    responseTarget = NULL;
}

void
OpaqueServerRPC::sendReply()
{
    {
        std::lock_guard<Core::Mutex> lock(ctxMutex);
        if (ctx) {
            IX::sendMessage(ctx, messageId, std::move(response));
        } else {
            response.reset();
        }
        // Prevent the server from replying again
        ctx = NULL;
    }
}

} // namespace LogCabin::RPC
} // namespace LogCabin
