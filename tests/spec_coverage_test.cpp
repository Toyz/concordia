#include "test_common.h"

TEST_F(ConcordiaTest, SpecCoverage_Match) {
    // @match is in the spec for telemetry
    CompileAndLoad(
        "packet P {"
        "  @match(0x42) uint8 type;"
        "}"
    );
    
}


