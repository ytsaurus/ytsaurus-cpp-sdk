#pragma once

#include <yt/yt/client/api/config.h>

#include <CXX/Objects.hxx> // pycxx

namespace NYT::NPython {

////////////////////////////////////////////////////////////////////////////////

NApi::EConnectionType ParseConnectionType(const Py::Object& obj);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NPython