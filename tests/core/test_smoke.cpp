#include <cstring>

#include "doctest.h"
#include "lsq/version.hpp"

TEST_CASE("version string is set") {
    CHECK(std::strlen(lsq::versionString()) > 0);
}
