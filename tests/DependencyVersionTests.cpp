// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include <cstdlib>
#include <iostream>

#include "BigScreen/DependencyVersion.hpp"

namespace {
    void Expect(bool condition, const char* message)
    {
        if(condition)
            return;
        std::cerr << message << '\n';
        std::exit(1);
    }
}

int main()
{
    using BigScreen::VersionIsBelowRangeMinimum;
    using BigScreen::VersionSatisfiesRange;

    Expect(VersionSatisfiesRange("4.8.0", "^4.8.0"),
           "the exact Paper2 minimum must satisfy its caret range");
    Expect(VersionSatisfiesRange("4.9.2", "^4.8.0"),
           "a later compatible Paper2 release must satisfy the range");
    Expect(!VersionSatisfiesRange("4.7.0", "^4.8.0"),
           "an older Paper2 release must fail the range");
    Expect(!VersionSatisfiesRange("5.0.0", "^4.8.0"),
           "a future Paper2 major release must fail the current caret range");
    Expect(VersionSatisfiesRange("v0.4.55", "^0.4.54"),
           "BSML's leading-v compatible release must parse");
    Expect(!VersionSatisfiesRange("0.5.0", "^0.4.54"),
           "BSML's next minor release must not satisfy a zero-major caret range");

    Expect(VersionIsBelowRangeMinimum("4.7.0", "^4.8.0"),
           "an installed version below the minimum must request a popup");
    Expect(!VersionIsBelowRangeMinimum("4.8.0", "^4.8.0"),
           "the exact minimum must not request a popup");
    Expect(!VersionIsBelowRangeMinimum("5.0.0", "^4.8.0"),
           "a future major mismatch must not be mislabelled as outdated");
    Expect(!VersionIsBelowRangeMinimum("unknown", "^4.8.0"),
           "an unreadable version must not create an outdated popup");

    std::cout << "Dependency version diagnostics tests passed.\n";
    return 0;
}
