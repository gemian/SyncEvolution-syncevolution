/*
 * Copyright (C) 2013 Intel Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) version 3.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301  USA
 */
#ifndef INCL_SYNC_EVOLUTION_IDENTITY_PROVIDER
# define INCL_SYNC_EVOLUTION_IDENTITY_PROVIDER

#include <syncevo/util.h>

#include <string>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

extern const char USER_IDENTITY_PLAIN_TEXT[];
extern const char USER_IDENTITY_SYNC_CONFIG[];

struct UserIdentity; // from SyncConfig.h

struct Credentials
{
    std::string m_username;
    std::string m_password;
};

/**
 * Returns username/password for an identity. The password is the
 * string configured for it inside SyncEvolution. It may be empty and/or unset if
 * the plain text password comes from the identity provider.
 *
 * If the credentials cannot be retrieved, an error is thrown, so don't use this
 * in cases where a different authentication method might also work.
 */
Credentials IdentityProviderCredentials(const UserIdentity &identity,
                                        const InitStateString &password);

SE_END_CXX
#endif