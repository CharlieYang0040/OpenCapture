#pragma once

namespace opencapture {

void ExplainLastItem(const char* title, const char* body, bool allowWhenDisabled = false);
void BeginCard(const char* id);
void EndCard();

} // namespace opencapture
