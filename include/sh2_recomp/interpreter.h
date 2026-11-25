#ifndef SH2_RECOMP_INTERPRETER_H
#define SH2_RECOMP_INTERPRETER_H

#include "sh2_recomp/step.h"

#ifdef __cplusplus
extern "C" {
#endif

/* With no catalog, interpret the fetched word. With a catalog, interpret
 * only a guarded miss. The current word is fetched once in either case. */
Sh2StepResult sh2_step_with_fallback(Sh2Context *context, const Sh2Catalog *catalog,
                                      bool *used_interpreter);

#ifdef __cplusplus
}
#endif
#endif
