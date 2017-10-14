#include "base/posix/eintr_wrapper.h"
#include "base/files/file_path.h"

#if defined(OS_LINUX)
#include <limits>
#endif

#include <stddef.h>
#include <sys/types.h>
#include <unistd.h>

namespace base {	
bool ReadFromFD(int fd, char* buffer, size_t bytes) {
  size_t total_read = 0;
  while (total_read < bytes) {
    ssize_t bytes_read =
        HANDLE_EINTR(read(fd, buffer + total_read, bytes - total_read));
    if (bytes_read <= 0)
      break;
    total_read += bytes_read;
  }
  return total_read == bytes;
}

//minimum file IO
const FilePath::CharType kStringTerminator = FILE_PATH_LITERAL('\0');

FilePath::FilePath() {
}
FilePath::FilePath(const FilePath& that) : path_(that.path_) {
}
FilePath::FilePath(FilePath&& that) noexcept = default;

FilePath::FilePath(StringPieceType path) {
  path.CopyToString(&path_);
  StringType::size_type nul_pos = path_.find(kStringTerminator);
  if (nul_pos != StringType::npos)
    path_.erase(nul_pos, StringType::npos);
}
FilePath::~FilePath() {
}

//TODO(iyatomi): if necessary, port OpenFile/CloseFile, instead of fopen/fclose
bool ReadFileToStringWithMaxSize(const FilePath& path,
                                 std::string* contents,
                                 size_t max_size) {
  if (contents)
    contents->clear();

  // TODO(iyatomi): reevaluate that is needed for the library. 
  // omit check path contained parent path (..)
  // if (path.ReferencesParent())
  //  return false; 
  FILE* file = fopen(path.value().c_str(), "rb");
  if (!file) {
    return false;
  }

  const size_t kBufferSize = 1 << 16;
  char buf[kBufferSize];
  size_t len;
  size_t size = 0;
  bool read_status = true;

  // Many files supplied in |path| have incorrect size (proc files etc).
  // Hence, the file is read sequentially as opposed to a one-shot read.
  while ((len = fread(buf, 1, kBufferSize, file)) > 0) {
    if (contents)
      contents->append(buf, std::min(len, max_size - size));

    if ((max_size - size) < len) {
      read_status = false;
      break;
    }

    size += len;
  }
  read_status = read_status && !ferror(file);
  fclose(file);

  return read_status;
}

bool ReadFileToString(const FilePath& path, std::string* contents) {
  return ReadFileToStringWithMaxSize(path, contents,
                                     std::numeric_limits<size_t>::max());
}
}
