#include "base/posix/eintr_wrapper.h"
#include "base/files/file_path.h"

#if defined(OS_LINUX)
#include <limits>
#endif

#include "basis/syscall.h"

namespace base {	
#if !defined(WIN32)
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

bool WriteFileDescriptor(const int fd, const char* data, int size) {
  // Allow for partial writes.
  ssize_t bytes_written_total = 0;
  for (ssize_t bytes_written_partial = 0; bytes_written_total < size;
       bytes_written_total += bytes_written_partial) {
    bytes_written_partial =
        HANDLE_EINTR(write(fd, data + bytes_written_total,
                           size - bytes_written_total));
    if (bytes_written_partial < 0)
      return false;
  }

  return true;
}
int WriteFile(const FilePath& filename, const char* data, int size) {
  int fd = HANDLE_EINTR(creat(filename.value().c_str(), 0666));
  if (fd < 0)
    return -1;

  int bytes_written = WriteFileDescriptor(fd, data, size) ? size : -1;
  if (IGNORE_EINTR(close(fd)) < 0)
    return -1;
  return bytes_written;
}

bool DirectoryExists(const FilePath& path) {
  struct stat file_info;
  if (stat(path.value().c_str(), &file_info) != 0)
    return false;
  return S_ISDIR(file_info.st_mode);
}
#else
int WriteFile(const FilePath& filename, const char* data, int size) {
  return 0;
}

bool DirectoryExists(const FilePath& path) {
  return false;
}
#endif

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

//FYI(iyatomi): this is simplified from original implementation, 
//to only meet requirement of platform_thread_linux.cc. 
FilePath FilePath::Append(StringPieceType component) const {
  StringPieceType appended = component;
  StringType without_nuls;

  StringType::size_type nul_pos = component.find(kStringTerminator);
  if (nul_pos != StringPieceType::npos) {
    component.substr(0, nul_pos).CopyToString(&without_nuls);
    appended = StringPieceType(without_nuls);
  }

  FilePath new_path(path_);
  appended.AppendToString(&new_path.path_);
  return new_path;
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
#if defined(WIN32)
  FILE* file = fopen((const char *)path.value().c_str(), "rb");
#else
  FILE* file = fopen(path.value().c_str(), "rb");
#endif
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
