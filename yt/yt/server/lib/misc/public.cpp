#include "private.h"

#include "interned_attributes.h"

namespace NYT::NServer {

////////////////////////////////////////////////////////////////////////////////

const TString ExecProgramName("ytserver-exec");
const TString JobProxyProgramName("ytserver-job-proxy");

////////////////////////////////////////////////////////////////////////////////

const TString BanMessageAttributeName("ban_message");
const TString ConfigAttributeName("config");

////////////////////////////////////////////////////////////////////////////////

#define XX(camelCaseName, snakeCaseName) \
    REGISTER_INTERNED_ATTRIBUTE(snakeCaseName, EInternedAttributeKey::camelCaseName)

FOR_EACH_INTERNED_ATTRIBUTE(XX)

#undef XX

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NServer
