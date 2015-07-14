// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This class works with command lines: building and parsing.
// Arguments with prefixes ('--', '-', and on Windows, '/') are switches.
// Switches will precede all other arguments without switch prefixes.
// Switches can optionally have values, delimited by '=', e.g., "-switch=value".
// An argument of "--" will terminate switch parsing during initialization,
// interpreting subsequent tokens as non-switch arguments, regardless of prefix.

// There is a singleton read-only CommandLine that represents the command line
// that the current process was started with.  It must be initialized in main().

#ifndef CHROMIUM_COMMAND_LINE_HH__
#define CHROMIUM_COMMAND_LINE_HH__

#pragma once

#include <stddef.h>
#include <map>
#include <string>
#include <vector>

class CommandLine {
 public:
  typedef std::string StringType;

  typedef StringType::value_type CharType;
  typedef std::vector<StringType> StringVector;
  typedef std::map<std::string, StringType> SwitchMap;

  // A constructor for CommandLines that only carry switches and arguments.
  enum NoProgram { NO_PROGRAM };
  explicit CommandLine(NoProgram no_program);

  // Construct a new command line from an argument list.
  CommandLine(int argc, const CharType* const* argv);
  explicit CommandLine(const StringVector& argv);

  ~CommandLine();

  // Initialize the current process CommandLine singleton.
  static void Init(int argc, const char* const* argv);

  // Destroys the current process CommandLine singleton. This is necessary if
  // you want to reset the base library to its initial state (for example, in an
  // outer library that needs to be able to terminate, and be re-initialized).
  // If Init is called only once, as in main(), Reset() is not necessary.
  static void Reset();

  // Get the singleton CommandLine representing the current process's
  // command line. Note: returned value is mutable, but not thread safe;
  // only mutate if you know what you're doing!
  static CommandLine* ForCurrentProcess();

  static CommandLine FromString(const std::string& command_line);

  // Initialize from an argv vector.
  void InitFromArgv(int argc, const CharType* const* argv);
  void InitFromArgv(const StringVector& argv);

  // Constructs and returns the represented command line string.
  // CAUTION! This should be avoided because quoting behavior is unclear.
  StringType GetCommandLineString() const;

  // Returns the original command line string as a vector of strings.
  const StringVector& argv() const { return argv_; }

  // Returns true if this command line contains the given switch.
  // (Switch names are case-insensitive).
  bool HasSwitch(const std::string& switch_string) const;

  // Returns the value associated with the given switch. If the switch has no
  // value or isn't present, this method returns the empty string.
  std::string GetSwitchValueASCII(const std::string& switch_string) const;
  StringType GetSwitchValueNative(const std::string& switch_string) const;

  // Get a copy of all switches, along with their values.
  const SwitchMap& GetSwitches() const { return switches_; }

  // Append a switch [with optional value] to the command line.
  // Note: Switches will precede arguments regardless of appending order.
  void AppendSwitch(const std::string& switch_string);
  void AppendSwitchNative(const std::string& switch_string,
                          const StringType& value);
  void AppendSwitchASCII(const std::string& switch_string,
                         const std::string& value);

  // Copy a set of switches (and any values) from another command line.
  // Commonly used when launching a subprocess.
  void CopySwitchesFrom(const CommandLine& source,
                        const char* const switches[],
                        size_t count);

  // Get the remaining arguments to the command.
  StringVector GetArgs() const;

  // Append an argument to the command line. Note that the argument is quoted
  // properly such that it is interpreted as one argument to the target command.
  // AppendArg is primarily for ASCII; non-ASCII input is interpreted as UTF-8.
  // Note: Switches will precede arguments regardless of appending order.
  void AppendArg(const std::string& value);
  void AppendArgNative(const StringType& value);

  // Append the switches and arguments from another command line to this one.
  // If |include_program| is true, include |other|'s program as well.
  void AppendArguments(const CommandLine& other, bool include_program);

  // Insert a command before the current command.
  // Common for debuggers, like "valgrind" or "gdb --args".
  void PrependWrapper(const StringType& wrapper);

  // Initialize by parsing the given command line string.
  // The program name is assumed to be the first item in the string.
  void ParseFromString(const std::string& command_line);

 private:
  // Disallow default constructor; a program name must be explicitly specified.
  CommandLine();
  // Allow the copy constructor. A common pattern is to copy of the current
  // process's command line and then add some flags to it. For example:
  //   CommandLine cl(*CommandLine::ForCurrentProcess());
  //   cl.AppendSwitch(...);

  // The singleton CommandLine representing the current process's command line.
  static CommandLine* current_process_commandline_;

  // The argv array: { program, [(--|-|/)switch[=value]]*, [--], [argument]* }
  StringVector argv_;

  // Parsed-out switch keys and values.
  SwitchMap switches_;

  // The index after the program and switches, any arguments start here.
  size_t begin_args_;
};

#endif  // CHROMIUM_COMMAND_LINE_HH__
