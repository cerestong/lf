#include <stdio.h>
#include "lf/status.hh"

namespace lf
{

const char *Status::msgs[] = {
    "" // kNone
};

const char *Status::CopyState(const char *state)
{
  char* const result = new char[strlen(state) + 1];
  strcpy(result, state);
  return result; 
}

Status::Status(Code _code, SubCode _subcode, const Slice &msg, const Slice &msg2)
    : code_(_code), subcode_(_subcode)
{
  assert(code_ != kOk);
  size_t len1 = msg.size();
  size_t len2 = msg2.size();
  size_t size = len1 + (len2 ? (len2+2) : 0);

  char * const result = new char[size + 1];
  memcpy(result, msg.data(), len1);
  if (len2)
  {
    result[len1] = ':';
    result[1 + len1] = ' ';
    memcpy(result + 2 + len1, msg2.data(), len2);
  }
  result[size] = '\0';
  state_ = result;
}

std::string Status::to_string() const
{
  char tmp[30];
  const char *type;

  switch (code_)
  {
  case kOk:
    return "OK";
  case kNotFound:
    type = "NotFound: ";
    break;
  case kCorruption:
    type = "Corruption: ";
    break;
  case kNotSupported:
    type = "Not implemented: ";
    break;
  case kInvalidArgument:
    type = "Invalid argument: ";
    break;
  default:
    snprintf(tmp, sizeof(tmp), "Unknown code(%d): ", static_cast<int>(code()));
    type = tmp;
    break;
  }
  std::string result(type);
  if (subcode_ != kNone)
  {
    uint32_t index = static_cast<int32_t>(subcode_);
    assert(sizeof(msgs) > index);
    result.append(msgs[index]);
  }

  if (state_ != nullptr)
  {
    result.append(state_);
  }
  return result;
}

} // end namespace
