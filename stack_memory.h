#define STACK_MEMORY_H
#ifdef STACK_MEMORY_H

#include<iostream>

class StackMemory
{
    size_t size_;
    char *stack_;

public:
    explicit StackMemory(size_t size = 65536)
        : size_(size), stack_(new char[size]) {}

    ~StackMemory() { delete[] stack_; }

    StackMemory(const StackMemory &) = delete;
    StackMemory &operator=(const StackMemory &) = delete;

    StackMemory(StackMemory &&other) noexcept
        : size_(other.size_), stack_(other.stack_)
    {
        other.stack_ = nullptr;
        other.size_ = 0;
    }

    StackMemory &operator=(StackMemory &&other) noexcept
    {
        if (this != &other)
        {
            delete[] stack_;
            size_ = other.size_;
            stack_ = other.stack_;
            other.stack_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }

    char *top() { return stack_ + size_; }
};

#endif