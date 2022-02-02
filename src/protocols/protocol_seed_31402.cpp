/**
 * Copyright (c) 2011-2019 libbitcoin developers (see AUTHORS)
 *
 * This file is part of libbitcoin.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <bitcoin/network/protocols/protocol_seed_31402.hpp>

#include <cstddef>
#include <functional>
#include <string>
#include <boost/asio.hpp>
#include <bitcoin/system.hpp>
#include <bitcoin/network/define.hpp>
#include <bitcoin/network/messages/messages.hpp>
#include <bitcoin/network/net/net.hpp>
#include <bitcoin/network/p2p.hpp>
#include <bitcoin/network/protocols/protocol_timer.hpp>

namespace libbitcoin {
namespace network {

#define CLASS protocol_seed_31402
static const std::string protocol_name = "seed";

// TODO: manage timestamps (active channels are connected < 3 hours ago).

using namespace bc;
using namespace bc::system;
using namespace messages;
using namespace std::placeholders;

// Require three callbacks (or any error) before calling complete.
protocol_seed_31402::protocol_seed_31402(const session& session,
    channel::ptr channel)
  : protocol_timer(session, channel,
      session.settings().channel_germination(), false),
    events_(zero)
{
}

// Start sequence.
// ----------------------------------------------------------------------------

void protocol_seed_31402::start(result_handler handler)
{
    BC_ASSERT_MSG(stranded(), "stranded");

    if (is_zero(settings().host_pool_capacity))
    {
        handler(error::address_not_found);
        return;
    }

    protocol_events::start(BIND1(handle_complete, _1));
    SUBSCRIBE2(address, handle_receive_address, _1, _2);
    SUBSCRIBE2(address, handle_receive_get_address, _1, _2);
    SEND1(get_address{}, handle_send_get_address, _1);
}

// Protocol.
// ----------------------------------------------------------------------------

void protocol_seed_31402::handle_complete(const code& ec)
{
    LOG_DEBUG(LOG_NETWORK)
        << "Seeding complete [" << authority() << "]";

    // Could be timeout, failure or success.
    stop(ec);
}

void protocol_seed_31402::handle_receive_address(const code& ec,
    address::ptr message)
{
    BC_ASSERT_MSG(stranded(), "stranded");

    if (stopped(ec))
        return;

    if (ec)
    {
        set_event(ec);
        return;
    }

    LOG_DEBUG(LOG_NETWORK)
        << "Received addresses.";

    // TODO: manage timestamps (active channels are connected < 3 hours ago).
    saves(message->addresses, BIND1(handle_load_addresses, _1));    
}

void protocol_seed_31402::handle_load_addresses(const code& ec)
{
    BC_ASSERT_MSG(stranded(), "stranded");

    if (stopped(ec))
        return;

    if (ec)
    {
        set_event(ec);
        return;
    }

    LOG_DEBUG(LOG_NETWORK)
        << "Stored addresses.";

    if (++events_ == 3u)
        set_event(error::success);
}

void protocol_seed_31402::handle_receive_get_address(const code& ec,
    get_address::ptr)
{
    BC_ASSERT_MSG(stranded(), "stranded");

    if (stopped(ec))
        return;

    if (ec)
    {
        set_event(ec);
        return;
    }

    if (is_zero(settings().self.port()))
    {
        set_event(error::success);
        return;
    }

    LOG_DEBUG(LOG_NETWORK)
        << "Receive get_address.";

    SEND1(address{ { settings().self.to_address_item() } },
        handle_send_address, _1);
}

void protocol_seed_31402::handle_send_address(const code& ec)
{
    if (stopped(ec))
        return;

    if (ec)
    {
        set_event(ec);
        return;
    }

    LOG_DEBUG(LOG_NETWORK)
        << "Sent addresses.";

    if (++events_ == 3u)
        set_event(error::success);
}

void protocol_seed_31402::handle_send_get_address(const code& ec)
{
    BC_ASSERT_MSG(stranded(), "stranded");

    if (stopped(ec))
        return;

    if (ec)
    {
        set_event(ec);
        return;
    }

    LOG_DEBUG(LOG_NETWORK)
        << "Sent get_address.";

    if (++events_ == 3u)
        set_event(error::success);
}

const std::string& protocol_seed_31402::name() const
{
    return protocol_name;
}

} // namespace network
} // namespace libbitcoin
