
#include <cassert>
#include <cctype>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace std::literals;

static void print_usage()
{
    printf(R"^-^(
DESCRIPTION

    Calculates and displays the circumstances under which particular line
    number(s) are present when compiling the specified source file with respect
    to preprocessor definitions.

USAGE

    when_present.exe --lines <value>... --file <path>

ARGUMENTS

    lines
        The line number(s) to calculate

    file
        Path to the file to read from

)^-^");
}

struct conditional_block;

// Represents the entirety of the start of the conditional block to the '#endif'. E.g. the whole of:
//      #if ...
//      ...
//      #elif ...
//      ...
//      #else
//      ...
//      #endif
struct conditional
{
    int begin_line; // The location of the starting '#if((n)def)'
    int end_line; // The location of the terminating '#endif'

    std::vector<std::unique_ptr<conditional_block>> blocks;
};

// E.g. represents something like the following:
//      #if ...
//      ...
//      #endif
// Or:
//      #elif ...
//      ...
//      #elif ...
// Or combinations of these
struct conditional_block
{
    int begin_line; // E.g. the location of the starting '#if', '#ifdef', '#else', etc.
    int end_line;   // E.g. the location of the terminating '#endif', '#else', etc.
    std::string condition;

    std::vector<conditional> nested_conditionals;
};

static constexpr const char whitespace[] = " \t\v";

static void process_line(int line, const std::vector<conditional>& conditionals)
{
    for (auto& cond : conditionals)
    {
        if (cond.begin_line <= line && cond.end_line >= line)
        {
            // Figure out which block it's in
            for (auto& block : cond.blocks)
            {
                if (block->begin_line <= line && block->end_line > line)
                {
                    printf("REQUIRES TRUE (%4d):  %s\n", block->begin_line, block->condition.c_str());
                    process_line(line, block->nested_conditionals);

                    // Ignore later blocks as they don't affect definition
                    break;
                }
                else
                {
                    // Otherwise the condition must be false. This is still relevant!
                    printf("REQUIRES FALSE (%4d): %s\n", block->begin_line, block->condition.c_str());
                }
            }

            // Cannot occur in any other conditional in the list...
            break;
        }
    }
}

int main(int argc, char** argv)
{
    std::string filePath;
    std::vector<int> lines;

    auto begin = argv + 1;
    auto end = argv + argc;
    for (; begin != end; ++begin)
    {
        std::string_view arg = *begin;
        if (arg == "--file"sv)
        {
            if (!filePath.empty())
            {
                printf("ERROR: Path specified more than once\n");
                return print_usage(), 1;
            }

            ++begin;
            if (begin == end)
            {
                printf("ERROR: Missing path\n");
                return print_usage(), 1;
            }
            filePath = *begin;
        }
        else if (arg == "--lines"sv)
        {
            ++begin;
            for (; (begin != end) && (**begin != '-'); ++begin)
            {
                auto line = std::atoi(*begin);
                if (line <= 0)
                {
                    printf("ERROR: Invalid line number '%s'\n", *begin);
                    return print_usage(), 1;
                }
                lines.push_back(line);
            }

            if (begin == end)
            {
                break;
            }
            --begin;
        }
        else if (arg == "--help"sv)
        {
            return print_usage(), 0;
        }
        else
        {
            printf("ERROR: Unrecognized argument \"%.*s\"\n", (int)arg.size(), arg.data());
            return print_usage(), 1;
        }
    }

    if (filePath.empty())
    {
        printf("ERROR: Must specify file path\n");
        return print_usage(), 1;
    }
    else if (lines.empty())
    {
        printf("ERROR: Must specify line number(s)\n");
        return print_usage(), 1;
    }

    // Read the entire file one line at a time, generating a tree that describes preprocessor requirements
    std::ifstream stream(filePath);
    if (stream.fail())
    {
        printf("ERROR: Failed to open file \"%s\"\n", filePath.c_str());
        return 1;
    }

    std::vector<conditional> conditionals;
    std::vector<conditional*> stateStack;

    std::string currentLine;
    for (int currentLineNumber = 1, linesRead; stream.good(); currentLineNumber += linesRead)
    {
        linesRead = 1;
        // Lines that end with '\' get appended with the next line. This may have valuable information if the next line
        // is part of a preprocessor condition, so merge the two
        std::getline(stream, currentLine);
        while (stream.good() && !currentLine.empty() && (currentLine.back() == '\\'))
        {
            currentLine.push_back('\n');
            std::string next;
            std::getline(stream, next);
            currentLine += next;
            ++linesRead;
        }

        // Leading whitespace is ignored
        auto pos = currentLine.find_first_not_of(whitespace);
        if (pos == currentLine.npos)
        {
            continue;
        }

        // Preprocessor directives must be first (e.g. cannot be after any other statement)
        if (currentLine[pos] != '#')
        {
            continue;
        }

        // There can be space after the '#', e.g. "#   if ..."
        pos = currentLine.find_first_not_of(whitespace, pos + 1);
        if (pos == currentLine.npos)
        {
            // This is ill-formed, but ignore...
            continue;
        }

        // Determine which directive this is
        auto endPos = pos;
        for (; endPos < currentLine.length(); ++endPos)
        {
            if (!std::isalpha(currentLine[endPos]))
            {
                break;
            }
        }

        std::string_view directive{ currentLine.c_str() + pos, endPos - pos };
        if (directive == "if"sv || directive == "ifdef"sv || directive == "ifndef"sv)
        {
            // This is the start of a new, possibly nested, conditional
            if (stateStack.empty())
            {
                // This is "top level"
                conditionals.emplace_back();
                stateStack.push_back(&conditionals.back());
            }
            else
            {
                // Nested inside of another conditional block
                auto& currentBlock = stateStack.back()->blocks.back();
                currentBlock->nested_conditionals.emplace_back();
                stateStack.push_back(&currentBlock->nested_conditionals.back());
            }

            auto& cond = *stateStack.back();
            assert(cond.blocks.empty());
            cond.begin_line = currentLineNumber;
            cond.blocks.emplace_back(std::make_unique<conditional_block>());
            cond.blocks.back()->begin_line = currentLineNumber;
            cond.blocks.back()->condition = currentLine;
        }
        else if (directive == "else"sv || directive == "elif"sv)
        {
            if (stateStack.empty())
            {
                std::wcout << L"ERROR: Encountered else outside of a conditional\n";
                return -1;
            }

            auto& cond = *stateStack.back();
            assert(!cond.blocks.empty());
            cond.blocks.back()->end_line = currentLineNumber;

            cond.blocks.emplace_back(std::make_unique<conditional_block>());
            cond.blocks.back()->begin_line = currentLineNumber;
            cond.blocks.back()->condition = currentLine;
        }
        else if (directive == "endif"sv)
        {
            // End of the current conditional
            if (stateStack.empty())
            {
                std::wcout << L"ERROR: Encountered '#endif' with no matching conditional\n";
                return -1;
            }

            auto& cond = *stateStack.back();
            assert(!cond.blocks.empty());
            cond.end_line = currentLineNumber;
            cond.blocks.back()->end_line = currentLineNumber;
            stateStack.pop_back();
        }
        // Otherwise, something we don't care about, e.g. pragma define, etc.
    }

    if (!stateStack.empty())
    {
        std::wcout << L"ERROR: Reached end of file with an active conditional block\n";
        return -1;
    }

    for (auto line : lines)
    {
        printf("Requirements for line %d being included in the translation unit:\n", line);
        process_line(line, conditionals);
        printf("\n");
    }
}
