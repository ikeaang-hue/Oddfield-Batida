#include "Cli.h"

#include <iostream>

int main (int argc, char* argv[])
{
    return batida::cli::run (juce::StringArray (argv + 1, argc - 1), std::cout, std::cerr);
}
