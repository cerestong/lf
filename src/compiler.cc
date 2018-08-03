#include "lf/compiler.hh"
#include <stdio.h>
#include <stdlib.h>

namespace lf
{

void fail_lf_invariant(const char *file, int line,
                       const char *assertion, const char *message)
{
    if (message)
        fprintf(stderr, "invariant \"%s\" [%s] failed: file \"%s\", line %d\n",
                message, assertion, file, line);
    else
        fprintf(stderr, "invariant \"%s\" failed: file \"%s\", line %d\n",
                assertion, file, line);
    abort();
}

void fail_lf_precondition(const char *file, int line,
                          const char *assertion, const char *message)
{
    if (message)
        fprintf(stderr, "precondition \"%s\" [%s] failed: file \"%s\", line %d\n",
                message, assertion, file, line);
    else
        fprintf(stderr, "precondition \"%s\" failed: file \"%s\", line %d\n",
                assertion, file, line);
    abort();
}

} // namespace lf
