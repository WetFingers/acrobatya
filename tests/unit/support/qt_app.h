// Qt для unit-тестов служб: один QCoreApplication на процесс (сигналы из потоков документа и пула приходят
// в главный поток через QueuedConnection — их доставляет цикл событий) и ожидание условия с циклом событий.
#pragma once

#include <functional>

class QCoreApplication;

namespace ot::test {

// QCoreApplication процесса (создаётся при первом вызове, в главном потоке gtest).
QCoreApplication& qtApp();

// Крутить цикл событий главного потока, пока pred() не вернёт true или не пройдёт timeoutMs.
// true — дождались.
bool waitUntil(const std::function<bool()>& pred, int timeoutMs = 5000);

}  // namespace ot::test
