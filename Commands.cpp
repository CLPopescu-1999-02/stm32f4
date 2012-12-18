#include "Commands.h"


char const * const CmdHelp::NAME[] = { "help", "?" };
char const * const CmdHelp::ARGV[] = { "os:command" };

char const * const CmdRead::NAME[] = { "read", "rb", "rh", "rw" };
char const * const CmdRead::ARGV[] = { "u:address", "ou:count" };

char const * const CmdWrite::NAME[] = { "write", "wb", "wh", "ww" };
char const * const CmdWrite::ARGV[] = { "u:address", "u:data" };


CmdHelp::CmdHelp() : Command(NAME, sizeof(NAME) / sizeof(NAME[0]), ARGV, sizeof(ARGV) / sizeof(ARGV[0]))
{
}

bool CmdHelp::execute(CommandInterpreter &interpreter, int argc, const CommandInterpreter::Argument *argv[])
{
    if (argc > 2) return false;
    char const * argCmd = nullptr;
    unsigned int argCmdLen = 0;
    if (argc == 2)
    {
        argCmd = argv[1]->value.s;
        argCmdLen = strlen(argv[1]->value.s);
    }
    for (auto i : interpreter)
    {
        CommandInterpreter::Command* cmd = i;
        if (argc != 2 || cmd->startsWith(argCmd, argCmdLen) != nullptr)
        {
            interpreter.printAliases(cmd);
            printf(" ");
            interpreter.printArguments(cmd, true);
            printf("\n");
            interpreter.printArguments(cmd, false);
            printf("\n  %s\n", cmd->helpText());
        }
    }
    return true;
}

CmdRead::CmdRead() : Command(NAME, sizeof(NAME) / sizeof(NAME[0]), ARGV, sizeof(ARGV) / sizeof(ARGV[0]))
{
}

bool CmdRead::execute(CommandInterpreter &interpreter, int argc, const CommandInterpreter::Argument *argv[])
{
    return false;
}

CmdWrite::CmdWrite() : Command(NAME, sizeof(NAME) / sizeof(NAME[0]), ARGV, sizeof(ARGV) / sizeof(ARGV[0]))
{
}

bool CmdWrite::execute(CommandInterpreter &interpreter, int argc, const CommandInterpreter::Argument *argv[])
{
    return false;
}

