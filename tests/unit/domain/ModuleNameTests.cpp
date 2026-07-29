#include "dvs/domain/Module.h"

#include <gtest/gtest.h>
#include <string_view>

namespace dvs::domain {

TEST(ModuleNameTests, ReturnsDomainTargetName) {
    EXPECT_EQ(moduleName(), std::string_view{"dvs_domain"});
}

} // namespace dvs::domain
