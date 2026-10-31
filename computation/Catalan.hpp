#pragma once
#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <vector>
// Small unsigned arbitrary-precision integer: no overflow and no extra dependency.
class Natural
{
    static constexpr std::uint64_t base = 1000000000;
    std::vector<std::uint32_t> digits_;

public:
    Natural(unsigned n = 0) : digits_{n} {}
    Natural &operator+=(const Natural &other)
    {
        digits_.resize(std::max(digits_.size(), other.digits_.size()), 0);
        std::uint64_t carry = 0;
        for (std::size_t i = 0; i < digits_.size(); ++i)
        {
            auto sum = carry + digits_[i] + (i < other.digits_.size() ? other.digits_[i] : 0);
            digits_[i] = sum % base;
            carry = sum / base;
        }
        if (carry)
            digits_.push_back(carry);
        return *this;
    }
    friend Natural operator*(const Natural &a, const Natural &b)
    {
        Natural out;
        out.digits_.assign(a.digits_.size() + b.digits_.size(), 0);
        for (std::size_t i = 0; i < a.digits_.size(); ++i)
        {
            std::uint64_t carry = 0;
            for (std::size_t j = 0; j < b.digits_.size(); ++j)
            {
                auto total = out.digits_[i + j] + carry + std::uint64_t(a.digits_[i]) * b.digits_[j];
                out.digits_[i + j] = total % base;
                carry = total / base;
            }
            out.digits_[i + b.digits_.size()] = carry;
        }
        while (out.digits_.size() > 1 && out.digits_.back() == 0)
            out.digits_.pop_back();
        return out;
    }
    std::string str() const
    {
        std::ostringstream out;
        out << digits_.back();
        for (auto i = digits_.size() - 1; i > 0; --i)
            out << std::setw(9) << std::setfill('0') << digits_[i - 1];
        return out.str();
    }
};
inline Natural countBinaryTreesNaive(int n)
{
    if (n <= 0)
        return Natural(1);
    Natural total;
    for (int i = 0; i < n; ++i)
        total += countBinaryTreesNaive(i) * countBinaryTreesNaive(n - 1 - i);
    return total;
}
