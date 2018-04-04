#pragma once

#include <string>
#include "lf/slice.hh"

namespace lf
{

class Status
{
public:
  enum Code
  {
    kOk = 0,
    kNotFound = 1,
    kCorruption = 2,
    kNotSupported = 3,
    kInvalidArgument = 4,
    kIOError = 5,
    kOverflow = 6, // new add from here
  };
  enum SubCode
  {
    kNone = 0,
    kMaxSubCode
  };

private:
  // A NULL state_ (which is always the case for OK) means the message
  // is empty.
  // of the following form:
  //    state_[0..3] == length of message
  //    state_[4..]  == message
  Code code_;
  SubCode subcode_;
  const char *state_;

  static const char *msgs[static_cast<int>(kMaxSubCode)];

public:
  Status() : code_(kOk), subcode_(kNone), state_(nullptr) {}
  ~Status() { delete[] state_; }

  // copy
  Status(const Status &s);
  Status &operator=(const Status &s);
  Status(Status &&s) noexcept;
  Status &operator=(Status &&s) noexcept;

  bool operator==(const Status &rhs) const;
  bool operator!=(const Status &rhs) const;

  Code code() const { return code_; }

  SubCode subcode() const { return subcode_; }

  const char *getstate() const { return state_; }

  // return a success status.
  static Status OK() { return Status(); }

  // return error status of an appropriate type.
  static Status NotFound(const Slice &msg, const Slice &msg2 = Slice())
  {
    return Status(kNotFound, msg, msg2);
  }

  // fast path for not found without maic
  static Status NotFound(SubCode msg = kNone)
  {
    return Status(kNotFound, msg);
  }

  static Status Corruption(const Slice &msg, const Slice &msg2 = Slice())
  {
    return Status(kCorruption, msg, msg2);
  }

  static Status
  Corruption(SubCode msg = kNone)
  {
    return Status(kCorruption, msg);
  }

  static Status NotSupported(const Slice &msg, const Slice &msg2 = Slice())
  {
    return Status(kNotSupported, msg, msg2);
  }

  static Status NotSupported(SubCode msg = kNone)
  {
    return Status(kNotSupported, msg);
  }

  static Status InvalidArgument(const Slice &msg, const Slice &msg2 = Slice())
  {
    return Status(kInvalidArgument, msg, msg2);
  }
  static Status InvalidArgument(SubCode msg = kNone)
  {
    return Status(kInvalidArgument, msg);
  }

  static Status IOError(const Slice &msg, const Slice &msg2 = Slice())
  {
    return Status(kIOError, msg, msg2);
  }
  static Status IOError(SubCode msg = kNone) { return Status(kIOError, msg); }

  bool ok() const { return code() == kOk; }
  bool not_found() const { return code() == kNotFound; }
  bool corruption() const { return code() == kCorruption; }
  bool not_supported() const { return code() == kNotSupported; }
  bool invalid_argument() const { return code() == kInvalidArgument; }
  bool io_error() const { return code() == kIOError; }

  // return a string representation of this status suitable for printing.
  // return the string "OK" for success.
  std::string to_string() const;

private:
  explicit Status(Code _code, SubCode _subcode = kNone)
      : code_(_code), subcode_(_subcode), state_(nullptr) {}

  Status(Code _code, SubCode _subcode, const Slice &msg, const Slice &msg2);
  Status(Code _code, const Slice &msg, const Slice &msg2)
      : Status(_code, kNone, msg, msg2) {}

  static const char *CopyState(const char *s);
};

inline Status &Status::operator=(const Status &s)
{
  if (this != &s)
  {
    code_ = s.code_;
    subcode_ = s.subcode_;
    delete[] state_;
    state_ = (s.state_ == nullptr) ? nullptr : CopyState(s.state_);
  }
  return *this;
}

inline Status::Status(Status &&s) noexcept
  : Status()
{
  *this = std::move(s);
}

inline Status & Status::operator=(Status &&s) noexcept
{
  if (this != &s) {
    code_ = std::move(s.code_);
    s.code_ = kOk;
    subcode_ = std::move(s.subcode_);
    s.subcode_ = kNone;
    delete[] state_;
    state_ = nullptr;
    std::swap(state_, s.state_);
  }
  return *this;
}

inline bool Status::operator==(const Status &rhs) const
{
  return (code_ == rhs.code_);
}

inline bool Status::operator!=(const Status &rhs) const
{
  return !(*this == rhs);
}

inline Status::Status(const Status &s)
    : code_(s.code_),
      subcode_(s.subcode_)
{
  state_ = (s.state_ == nullptr) ? nullptr : CopyState(s.state_);
}

} // end namespace
