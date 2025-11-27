#include "test_common.h"

TEST_F(ConcordiaTest, SpecCoverage_Unit) {
    // @unit is in the spec but was missing from implementation
    CompileAndLoad(
        "packet P {"
        "  @unit(\"seconds\") uint32 duration;"
        "}"
    );
    // If compilation succeeds, we are good.
}

TEST_F(ConcordiaTest, SpecCoverage_Match) {
    // @match is in the spec for telemetry
    CompileAndLoad(
        "packet P {"
        "  @match(0x42) uint8 type;"
        "}"
    );
    
}


