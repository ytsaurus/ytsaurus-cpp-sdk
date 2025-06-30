#pragma once

#include "public.h"

#include <yt/yt/client/api/public.h>

namespace NYT::NAuth {

////////////////////////////////////////////////////////////////////////////////

ICookieAuthenticatorPtr CreateCypressCookieAuthenticator(
    TCypressCookieGeneratorConfigPtr config,
    ICypressCookieStorePtr cookieStore,
    NApi::IClientPtr client);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NAuth
