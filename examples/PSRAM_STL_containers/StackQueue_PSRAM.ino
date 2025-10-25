#define RF_USE_PSRAM
#include <Arduino.h>
#include <STL_MCU.h>

using namespace mcu;

namespace {

void drainStack(Stack<int>& stack) {
    Serial.print("Popping sequence: ");
    while (!stack.empty()) {
        Serial.print(stack.pop());
        Serial.print(' ');
    }
    Serial.println();
}

void drainQueue(Queue<int>& queue) {
    Serial.print("Remaining queue: ");
    while (!queue.empty()) {
        Serial.print(queue.dequeue());
        Serial.print(' ');
    }
    Serial.println();
}

void drainDeQueueFront(DeQueue<int>& dq) {
    Serial.print("Drain front: ");
    while (!dq.empty()) {
        Serial.print(dq.dequeueFront());
        Serial.print(' ');
    }
    Serial.println();
}

void drainDeQueueBack(DeQueue<int>& dq) {
    Serial.print("Drain back: ");
    while (!dq.empty()) {
        Serial.print(dq.dequeueBack());
        Serial.print(' ');
    }
    Serial.println();
}

void testStack() {
    Serial.println("=== Stack test ===");
    Stack<int> stack;

    for (int i = 0; i < 32; ++i) {
        stack.push(i);
    }
    Serial.print("Stack size after push: ");
    Serial.println(stack.getSize());
    Serial.print("Stack uses PSRAM: ");
    Serial.println(stack.uses_psram() ? "yes" : "no");

    drainStack(stack);

    for (int i = 100; i < 108; ++i) {
        stack.push(i);
    }
    Serial.print("Stack size after reuse: ");
    Serial.println(stack.getSize());
    Serial.print("Top element: ");
    Serial.println(stack.top());

    stack.clear();
    Serial.print("Stack empty after clear: ");
    Serial.println(stack.empty() ? "yes" : "no");
    Serial.println();
}

void testQueue() {
    Serial.println("=== Queue test ===");
    Queue<int> queue;

    for (int i = 0; i < 40; ++i) {
        queue.enqueue(i);
    }
    Serial.print("Queue size after enqueue: ");
    Serial.println(queue.getSize());
    Serial.print("Queue uses PSRAM: ");
    Serial.println(queue.uses_psram() ? "yes" : "no");

    Serial.print("Dequeued values: ");
    for (int i = 0; i < 20; ++i) {
        Serial.print(queue.dequeue());
        Serial.print(' ');
    }
    Serial.println();

    for (int i = 100; i < 112; ++i) {
        queue.enqueue(i);
    }
    Serial.print("Queue size after reuse: ");
    Serial.println(queue.getSize());
    Serial.print("Queue front: ");
    Serial.println(queue.front());

    drainQueue(queue);
    Serial.println();
}

void testDeQueue() {
    Serial.println("=== DeQueue test ===");
    DeQueue<int> dq;

    for (int i = 0; i < 12; ++i) {
        dq.enqueueBack(i);
    }
    for (int i = 100; i < 106; ++i) {
        dq.enqueueFront(i);
    }
    Serial.print("DeQueue size after enqueue: ");
    Serial.println(dq.getSize());
    Serial.print("DeQueue uses PSRAM: ");
    Serial.println(dq.uses_psram() ? "yes" : "no");
    Serial.print("Front value: ");
    Serial.println(dq.front());
    Serial.print("Back value: ");
    Serial.println(dq.back());

    drainDeQueueFront(dq);

    for (int i = 0; i < 6; ++i) {
        dq.enqueueBack(i * 3);
    }
    for (int i = 0; i < 3; ++i) {
        dq.enqueueFront(-i);
    }
    Serial.print("DeQueue size after reuse: ");
    Serial.println(dq.getSize());
    Serial.print("Front value: ");
    Serial.println(dq.front());
    Serial.print("Back value: ");
    Serial.println(dq.back());

    drainDeQueueBack(dq);
    Serial.println();
}

} // namespace

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000) {
        delay(10);
    }

    Serial.println();
    Serial.println("Stack/Queue/DeQueue PSRAM exercise");
#if RF_PSRAM_AVAILABLE
    Serial.print("Free PSRAM before tests: ");
    Serial.println(mcu::mem_alloc::get_free_psram());
#else
    Serial.println("PSRAM not available on this build.");
#endif

    testStack();
    testQueue();
    testDeQueue();

#if RF_PSRAM_AVAILABLE
    Serial.print("Free PSRAM after tests: ");
    Serial.println(mcu::mem_alloc::get_free_psram());
#endif

    Serial.println("Tests complete.");
}

void loop() {
    // Intentionally left empty.
}
