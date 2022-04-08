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
#ifndef LIBBITCOIN_NETWORK_ASYNC_ENABLE_SHARED_FROM_BASE_HPP
#define LIBBITCOIN_NETWORK_ASYNC_ENABLE_SHARED_FROM_BASE_HPP

#include <memory>
#include <bitcoin/system.hpp>

namespace libbitcoin {
namespace network {

/// Because enable_shared_from_this doesn't support inheritance.
template <class Base>
class enable_shared_from_base
  : public std::enable_shared_from_this<Base>
{
public:
    // Simplifies capture of the shared pointer for a nop handler.
    void nop() volatile noexcept
    {
    }

protected:
    template <class Derived, system::if_base_of<Base, Derived> = true>
    std::shared_ptr<Derived> shared_from_base() noexcept
    {
        // Instance (not just type) must be upcastable to Derived.
        return std::static_pointer_cast<Derived>(this->shared_from_this());
    }
};

} // namespace network
} // namespace libbitcoin

#endif
