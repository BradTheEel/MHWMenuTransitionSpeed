#ifndef QUEST_STUB_H
#define QUEST_STUB_H
#include <stdint.h>
typedef float (*QuestRateHelper)(uintptr_t action, float nativeFactor);
typedef struct QuestStub QuestStub;
/* Returns nonzero only after allocation, code protection, and unwind registration. */
int CreateQuestStub(uintptr_t gameBase, QuestRateHelper helper, QuestStub **out);
void *QuestStubEntry(const QuestStub *stub);
/* Caller must first make the stub unreachable and wait for all executions to finish. */
void DestroyQuestStub(QuestStub *stub);
#endif
