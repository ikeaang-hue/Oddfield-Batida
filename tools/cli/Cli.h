#pragma once

#include <juce_core/juce_core.h>

#include <ostream>

// batida: Batida's files and engine from the command line (SPEC §13).
//
//   batida render <file> -o out.wav     a sound, kit, pattern or set, through the whole chain
//   batida analyze <file.wav>           what a render measures
//   batida validate <file>...           everything loading lets slide
//   batida describe <file>              a plain summary
//   batida new <type> <name>            a file to start from
//   batida list                         the library
//   batida params                       every setting
//
// Text for people by default, --json for programs. Exit codes: 0 ok, 1 a
// check failed (a problem found, a silent render), 2 bad arguments or a file
// that can't be read.

namespace batida::cli
{

int run (const juce::StringArray& args, std::ostream& out, std::ostream& err);

} // namespace batida::cli
