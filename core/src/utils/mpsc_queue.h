/* 
 * This file is part of the SDRPP distribution (https://github.com/qrp73/SDRPP).
 * Copyright (c) 2025 qrp73.
 * 
 * This program is free software: you can redistribute it and/or modify  
 * it under the terms of the GNU General Public License as published by  
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU 
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License 
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#pragma once
#include <atomic>


// lock-free Multiple Producer Single Consumer (MPSC) FIFO queue (Michael-Scott)
template<typename T>
class mpsc_queue {

private:
    struct Node {
        T value;
        std::atomic<Node*> next{nullptr};
        Node() = default;
        Node(const T& v) : value(v) {}
        Node(T&& v) : value(std::move(v)) {}
    };

    std::atomic<Node*> head; // producers push here
    Node* tail;              // consumer pops from tail
    std::atomic<ssize_t> count{0}; // approximate size

public:
    mpsc_queue() {
        Node* dummy = new Node(); // dummy node
        head.store(dummy);
        tail = dummy;
    }

    ~mpsc_queue() {
        Node* node = tail;
        while (node) {
            Node* next = node->next.load();
            delete node;
            node = next;
        }
    }

    void enqueue(const T& val) {
        Node* node = new Node(val);
        Node* prev = head.exchange(node, std::memory_order_acq_rel);
        prev->next.store(node, std::memory_order_release);
        count.fetch_add(1, std::memory_order_relaxed);
    }

    void enqueue(T&& val) {
        Node* node = new Node(std::move(val));
        Node* prev = head.exchange(node, std::memory_order_acq_rel);
        prev->next.store(node, std::memory_order_release);
        count.fetch_add(1, std::memory_order_relaxed);
    }

    bool try_dequeue(T& out) {
        Node* next = tail->next.load(std::memory_order_acquire);
        if (!next) return false; // queue empty

        out = std::move(next->value);
        delete tail;
        tail = next;
        count.fetch_sub(1, std::memory_order_relaxed);
        return true;
    }

    // Note: size() is approximate. It may be nonzero when try_dequeue() returns false,
    // due to relaxed memory ordering between producers and the consumer.
    // Usage: memory back pressure detection
    size_t size() const noexcept {
        auto v = count.load(std::memory_order_relaxed);
        return v < 0 ? 0 : v;
    }
};
