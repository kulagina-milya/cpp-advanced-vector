#pragma once
#include <cassert>
#include <cstdlib>
#include <new>
#include <utility>
#include <memory>
#include <iterator>

template <typename T>
class RawMemory {
public:
    RawMemory() = default;

    explicit RawMemory(size_t capacity)
        : buffer_(Allocate(capacity))
        , capacity_(capacity) {
    }

    ~RawMemory() {
        Deallocate(buffer_);
    }

    RawMemory(const RawMemory&) = delete;
    RawMemory& operator=(const RawMemory& rhs) = delete;

    RawMemory(RawMemory&& other) noexcept
        : buffer_(std::exchange(other.buffer_, nullptr)),
        capacity_(std::exchange(other.capacity_, 0))
    {
    }

    RawMemory& operator=(RawMemory&& rhs) noexcept {
        if (this != &rhs) {
            Deallocate(buffer_);
            buffer_ = std::exchange(rhs.buffer_, nullptr);
            capacity_ = std::exchange(rhs.capacity_, 0);
        }
        return *this;
    }

    T* operator+(size_t offset) noexcept {
        // Разрешается получать адрес ячейки памяти, следующей за последним элементом массива
        assert(offset <= capacity_);
        return buffer_ + offset;
    }

    const T* operator+(size_t offset) const noexcept {
        return const_cast<RawMemory&>(*this) + offset;
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<RawMemory&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < capacity_);
        return buffer_[index];
    }

    void Swap(RawMemory& other) noexcept {
        std::swap(buffer_, other.buffer_);
        std::swap(capacity_, other.capacity_);
    }

    const T* GetAddress() const noexcept {
        return buffer_;
    }

    T* GetAddress() noexcept {
        return buffer_;
    }

    size_t Capacity() const {
        return capacity_;
    }

private:
    // Выделяет сырую память под n элементов и возвращает указатель на неё
    static T* Allocate(size_t n) {
        return n != 0 ? static_cast<T*>(operator new(n * sizeof(T))) : nullptr;
    }

    // Освобождает сырую память, выделенную ранее по адресу buf при помощи Allocate
    static void Deallocate(T* buf) noexcept {
        operator delete(buf);
    }

    T* buffer_ = nullptr;
    size_t capacity_ = 0;
};


template <typename T>
class Vector {
public:
    Vector() = default;

    explicit Vector(size_t size)
        : data_(size),
        size_(size)  //
    {
        std::uninitialized_value_construct_n(data_.GetAddress(), size);
    }

    Vector(const Vector& other)
        : data_(other.size_),
        size_(other.size_)  //
    {
        std::uninitialized_copy_n(other.data_.GetAddress(), size_, data_.GetAddress());
    }

    Vector(Vector&& other) noexcept
        : data_(std::exchange(other.data_, RawMemory<T>{})),
        size_(std::exchange(other.size_, 0))
    {
    }

    Vector& operator=(const Vector& rhs) {
        if (this != &rhs) {
            if (rhs.size_ <= Capacity()) {
                if (size_ <= rhs.size_) {
                    std::copy(rhs.data_.GetAddress(), rhs.data_.GetAddress() + size_,
                        data_.GetAddress());
                    std::uninitialized_copy_n(
                        rhs.data_.GetAddress() + size_, rhs.size_ - size_,
                        data_.GetAddress() + size_);
                }
                else {
                    std::copy(rhs.data_.GetAddress(), rhs.data_.GetAddress() + rhs.size_,
                        data_.GetAddress());
                    std::destroy_n(data_.GetAddress() + rhs.size_,
                        size_ - rhs.size_);
                }
                size_ = rhs.size_;
            }
            else {
                Vector other_copy(rhs);
                Swap(other_copy);
            }
        }
        return *this;
    }

    Vector& operator=(Vector&& rhs) noexcept {
        Swap(rhs);
        return *this;
    }

    void Swap(Vector& other) noexcept {
        data_.Swap(other.data_);
        std::swap(size_, other.size_);
    }

    size_t Size() const noexcept {
        return size_;
    }

    size_t Capacity() const noexcept {
        return data_.Capacity();
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<Vector&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < size_);
        return data_[index];
    }

    void Reserve(size_t new_capacity) {
        if (new_capacity <= Capacity()) {
            return;
        }
        RawMemory<T> new_data(new_capacity);
        if constexpr
            (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(data_.GetAddress(), size_, new_data.GetAddress());
        }
        else {
            std::uninitialized_copy_n(data_.GetAddress(), size_, new_data.GetAddress());
        }

        std::destroy_n(data_.GetAddress(), size_);
        data_.Swap(new_data);
    }

    void Resize(size_t new_size) {
        if (new_size < size_) {
            std::destroy_n(data_.GetAddress() + new_size, size_ - new_size);
        }
        else {
            if (new_size > Capacity()) {
                Reserve(new_size);
            }
            std::uninitialized_value_construct_n(data_.GetAddress() + size_, new_size - size_);
        }
        size_ = new_size;
    }

    void PushBack(const T& value) {
        (void)EmplaceBack(value);
    }

    void PushBack(T&& value) {
        (void)EmplaceBack(std::move(value));
    }

    void PopBack() /* noexcept */ {
        assert(size_);
        std::destroy_at(data_.GetAddress() + size_ - 1);
        --size_;
    }

    template <typename... Args>
    T& EmplaceBack(Args&&... args) {
        if (Capacity() <= size_) {
            RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);
            new (new_data.GetAddress() + size_) T(std::forward<Args>(args)...);
            try {
                if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
                    std::uninitialized_move_n(data_.GetAddress(), size_, new_data.GetAddress());
                }
                else {
                    std::uninitialized_copy_n(data_.GetAddress(), size_, new_data.GetAddress());
                }
            } catch (...) {
                std::destroy_at(new_data + size_);
                throw;
            }
            std::destroy_n(data_.GetAddress(), size_);
            data_.Swap(new_data);
        }
        else {
            new (data_.GetAddress() + size_) T(std::forward<Args>(args)...);
        }
        return data_[size_++];
    }

    using iterator = T*;
    using const_iterator = const T*;

    iterator begin() noexcept {
        return data_.GetAddress();
    }

    iterator end() noexcept {
        return data_.GetAddress() + size_;
    }

    const_iterator begin() const noexcept {
        return data_.GetAddress();
    }

    const_iterator end() const noexcept {
        return data_.GetAddress() + size_;
    }

    const_iterator cbegin() const noexcept {
        return begin();
    }

    const_iterator cend() const noexcept {
        return end();
    }

    template <typename... Args>
    iterator Emplace(const_iterator pos, Args&&... args) {
        assert(pos >= begin() && pos <= end());
        auto distance = std::distance(cbegin(), pos);

        if (Capacity() <= size_) {
            RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);
            new (new_data.GetAddress() + distance) T(std::forward<Args>(args)...);
            try {
                if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
                    std::uninitialized_move_n(data_.GetAddress(), distance, new_data.GetAddress());
                    std::uninitialized_move_n(data_.GetAddress() + distance, size_ - distance,
                    new_data.GetAddress() + distance + 1);
                }
                else {
                    std::uninitialized_copy_n(data_.GetAddress(), distance, new_data.GetAddress());
                    std::uninitialized_copy_n(data_.GetAddress() + distance, size_ - distance,
                    new_data.GetAddress() + distance + 1);
                }
            } catch (...) {
                std::destroy_n(new_data.GetAddress(), distance + 1);
                throw;
            }
            std::destroy_n(data_.GetAddress(), size_);
            data_.Swap(new_data);
        }
        else {
                if (pos != end()) {
                    T new_value(std::forward<Args>(args)...);
                    new (end()) T(std::forward<T>(data_[size_ - 1]));
                    try {
                        std::move_backward(begin() + distance, end() - 1, end());
                    } catch (...) {
                        std::destroy_at(end());
                        throw;
                    }
                    
                    *(begin() + distance) = std::forward<T>(new_value);
                }
                else {
                    new (end()) T(std::forward<Args>(args)...);
                }
            
        }

        ++size_;
        return begin() + distance;
    }

    iterator Erase(const_iterator pos) noexcept(std::is_nothrow_move_assignable_v<T>) {
        assert(pos >= cbegin() && (pos < cbegin() + Size()));
        size_t distance = std::distance(cbegin(), pos);
        std::move(begin() + distance + 1, end(), begin() + distance);
        std::destroy_at(end() - 1);
        --size_;
        return (begin() + distance);
    }

    iterator Insert(const_iterator pos, const T& value) {
        return Emplace(pos, value);
    }

    iterator Insert(const_iterator pos, T&& value) {
        return Emplace(pos, std::move(value));
    }

    ~Vector() {
        std::destroy_n(data_.GetAddress(), size_);
    }

private:
    RawMemory<T> data_;
    size_t size_ = 0;
};