#pragma once

// The files jnext writes for an output option (--wav-record, --tape-save, --profile-output, ...),
// reported to whoever wants to list them. Only the browser build installs a sink (main(), under
// JNEXT_BROWSER): it hands each path to the page, whose player.listFiles() / readFile() serve
// them. Natively the sink is null, so every call below is a test of one pointer and nothing else.
//
// A writer calls begin() when it opens the file (more bytes will follow) and end() when it has
// finished it for good; written() is begin and end in one for a file written at once. The path is
// passed on exactly as the writer has it: the page canonicalises it with its own file system.
// An empty path (an option that was not given) sends nothing.

#include <string>

namespace output_files {

using Sink = void (*)(const char* path, bool complete);

inline Sink g_sink = nullptr;   // set only by the browser build

inline void set_sink(Sink s) { g_sink = s; }

inline void begin(const std::string& path)   { if (g_sink && !path.empty()) g_sink(path.c_str(), false); }
inline void end(const std::string& path)     { if (g_sink && !path.empty()) g_sink(path.c_str(), true); }
inline void written(const std::string& path) { end(path); }

}  // namespace output_files
