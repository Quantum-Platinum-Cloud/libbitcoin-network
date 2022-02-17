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
#include <bitcoin/network/net/hosts.hpp>

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>
#include <bitcoin/system.hpp>
#include <bitcoin/network/config/config.hpp>
#include <bitcoin/network/error.hpp>
#include <bitcoin/network/messages/messages.hpp>
#include <bitcoin/network/settings.hpp>

namespace libbitcoin {
namespace network {

using namespace bc::system;
using namespace config;
using namespace messages;

#define NAME "hosts"

inline bool is_invalid(const address_item& host)
{
    return is_zero(host.port) || host.ip == null_ip_address;
}

// TODO: change to network_address bimap hash table with services and age.
hosts::hosts(const settings& settings)
  : disabled_(is_zero(settings.host_pool_capacity)),
    capacity_(static_cast<size_t>(settings.host_pool_capacity)),
    file_path_(settings.hosts_file),
    count_(zero),
    buffer_(std::max(capacity_, one)),
    stopped_(true)
{
}

hosts::~hosts()
{
    BC_ASSERT(stopped_);
}

size_t hosts::count() const
{
    return count_.load(std::memory_order_relaxed);
}

code hosts::start()
{
    if (disabled_)
        return error::success;

    if (!stopped_)
        return error::operation_failed;

    stopped_ = false;
    ifstream file(file_path_.string(), ifstream::in);

    // An invalid path or non-existent file will not cause an error on open.
    ////if (file.bad())
    ////{
    ////    LOG_DEBUG(LOG_NETWORK) << "Failed to load hosts file." << std::endl;
    ////    return error::file_load;
    ////}

    std::string line;
    while (std::getline(file, line))
    {
        // TODO: create full space-delimited network_address serialization.
        // Use to/from string format as opposed to wire serialization.
        config::authority entry(line);
        auto host = entry.to_address_item();

        if (!is_invalid(host))
            buffer_.push_back(host);
    }

    count_.store(buffer_.size(), std::memory_order_relaxed);
    return error::success;
}

code hosts::stop()
{
    if (disabled_)
        return error::success;

    if (stopped_)
        return error::success;

    stopped_ = true;
    ofstream file(file_path_.string(), ofstream::out);

    // An invalid path or non-existent file will not cause an error on open.
    ////if (file.bad())
    ////{
    ////    LOG_DEBUG(LOG_NETWORK) << "Failed to store hosts file." << std::endl;
    ////    return error::file_load;
    ////}

    for (const auto& entry: buffer_)
    {
        // TODO: create full space-delimited network_address serialization.
        // Use to/from string format as opposed to wire serialization.
        file << config::authority(entry) << std::endl;
    }

    // An invalid path or non-existent file will cause an error on write.
    if (file.bad())
    {
        LOG_DEBUG(LOG_NETWORK) << "Failed to store hosts file." << std::endl;
        return error::file_load;
    }

    buffer_.clear();
    count_.store(zero, std::memory_order_relaxed);
    return error::success;
}

void hosts::store(const address_item& host)
{
    if (disabled_ || stopped_)
        return;

    // Do not treat invalid address as an error, just log it.
    if (is_invalid(host))
    {
        LOG_DEBUG(LOG_NETWORK) << "Invalid host address from peer."
            << std::endl;
        return;
    }

    if (find(host) == buffer_.end())
    {
        buffer_.push_back(host);
        count_.store(buffer_.size(), std::memory_order_relaxed);
    }
}

void hosts::store(const address_items& hosts)
{
    if (disabled_ || stopped_ || hosts.empty())
        return;

    // Accept between 1 and all of this peer's addresses up to capacity.
    const auto capacity = buffer_.capacity();
    const auto usable = std::min(hosts.size(), capacity);
    const auto random = pseudo_random::next(one, usable);

    // But always accept at least the amount we are short if available.
    const auto gap = capacity - buffer_.size();
    const auto accept = std::max(gap, random);

    // Convert minimum desired to step for iteration, no less than 1.
    const auto step = std::max(usable / accept, one);
    size_t accepted = 0;

    for (size_t index = 0; index < usable; index = ceilinged_add(index, step))
    {
        const auto& host = hosts[index];

        // Do not treat invalid address as an error, just log it.
        if (is_invalid(host))
        {
            LOG_DEBUG(LOG_NETWORK) << "Invalid host address from peer."
                << std::endl;
            continue;
        }

        // TODO: use std::map.
        // Do not allow duplicates in the host cache.
        if (find(host) == buffer_.end())
        {
            ++accepted;
            buffer_.push_back(host);
            count_.store(buffer_.size(), std::memory_order_relaxed);
        }
    }

    LOG_VERBOSE(LOG_NETWORK)
        << "Accepted (" << accepted << " of " << hosts.size()
        << ") host addresses from peer." << std::endl;
}

void hosts::remove(const address_item& host)
{
    if (stopped_ || buffer_.empty())
        return;

    auto it = find(host);
    if (it == buffer_.end())
    {
        LOG_DEBUG(LOG_NETWORK) << "Address to remove not found." << std::endl;
        return;
    }

    buffer_.erase(it);
    count_.store(buffer_.size(), std::memory_order_relaxed);
}

void hosts::fetch(address_item_handler handler) const
{
    if (stopped_)
    {
        handler(error::service_stopped, {});
        return;
    }

    if (buffer_.empty())
    {
        handler(error::address_not_found, {});
        return;
    }

    // Randomly select an address from the buffer.
    const auto limit = sub1(buffer_.size());
    const auto index = pseudo_random::next(zero, limit);
    handler(error::success, buffer_[index]);
}

void hosts::fetch(address_items_handler handler) const
{
    if (stopped_)
    {
        handler(error::service_stopped, {});
        return;
    }

    if (buffer_.empty())
    {
        handler(error::address_not_found, {});
        return;
    }

    // TODO: extract 5/10 to configuration.
    const auto out_count = std::min(messages::max_address,
        buffer_.size() / pseudo_random::next<size_t>(5u, 10u));

    const auto limit = sub1(buffer_.size());
    auto index = pseudo_random::next(zero, limit);

    address_items out;
    out.reserve(out_count);
    for (size_t count = 0; count < out_count; ++count)
        out.push_back(buffer_[index++ % limit]);

    pseudo_random::shuffle(out);
    handler(error::success, out);
}

// private
hosts::buffer::iterator hosts::find(const address_item& host)
{
    const auto found = [&host](const address_item& entry)
    {
        // Message types do not implement comparison operators.
        return entry.port == host.port && entry.ip == host.ip;
    };

    return std::find_if(buffer_.begin(), buffer_.end(), found);
}

} // namespace network
} // namespace libbitcoin
