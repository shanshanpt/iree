#!/usr/bin/env python3

# Copyright 2020 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Prepares MarkDown documentation publication.

The in-tree and auto-generated MarkDown documentation lacks necessary metadata
(i.e., front matter) for specifying the layout, title, and others that are
required by Jekyll for rendering the HTML. This script patches MarkDown
files with that.
"""

import argparse
import os


def parse_arguments():
  """Parses command-line options."""
  parser = argparse.ArgumentParser(
      description='Processes MarkDown files for publication')
  parser.add_argument(
      'base_dir',
      metavar='PATH',
      type=str,
      help='Base documentation directory.')

  parsed_args = parser.parse_args()
  if not os.path.isdir(parsed_args.base_dir):
    raise parser.error('expected path to a directory')

  return parsed_args


# A dictionary containing source file to doc title mappings.
#
# By default the generated doc will have a title matching the MarkDown H1
# header. This dictionary will overrule that default behavior.
DOC_TITLE_DICT = {
    'index.md': 'Home',
    'getting_started_linux_bazel.md': 'Linux with Bazel',
    'getting_started_linux_cmake.md': 'Linux with CMake',
    'getting_started_linux_vulkan.md': 'Linux with Vulkan',
    'getting_started_windows_bazel.md': 'Windows with Bazel',
    'getting_started_windows_cmake.md': 'Windows with CMake',
    'getting_started_windows_vulkan.md': 'Windows with Vulkan',
    'generic_vulkan_env_setup.md': 'Generic Vulkan Setup',
    'getting_started_python.md': 'Python',
    'op_coverage.md': 'XLA HLO Operation Coverage',
    'roadmap.md': 'Short-term Focus Areas',
    'roadmap_design.md': 'Long-term Design Roadmap',
}

# A dictionary containing source file to permanent link mappings.
#
# By default a source file will have a permanent URL link following its
# filename. For example, if we have docs/Foo/Bar.md, then the permanent link
# for it would be https://google.github.io/iree/Foo/Bar. This dictionary
# allows one to override the permanent link if necessary.
PERMALINK_DICT = {
    'index.md': '/',
    'getting_started_linux_bazel.md': 'GetStarted/LinuxBazel',
    'getting_started_linux_cmake.md': 'GetStarted/LinuxCMake',
    'getting_started_linux_vulkan.md': 'GetStarted/LinuxVulkan',
    'getting_started_windows_bazel.md': 'GetStarted/WindowsBazel',
    'getting_started_windows_cmake.md': 'GetStarted/WindowsCMake',
    'getting_started_windows_vulkan.md': 'GetStarted/WindowsVulkan',
    'generic_vulkan_env_setup.md': 'GetStarted/GenericVulkanSetup',
    'getting_started_python.md': 'GetStarted/Python',
    'developer_overview.md': 'DeveloperOverview',
    'testing_guide.md': 'TestingGuide',
    'op_coverage.md': 'HLOOpCoverage',
    'roadmap.md': 'FocusAreas',
    'roadmap_design.md': 'DesignRoadmap',
}

# A dictionary containing source file to navigation order mappings.
#
# By default the rendered docs will be sort alphabetically and listed on
# the left panel of https://google.github.io/iree website. This allows one
# to specify an order for a specific doc.
NAVI_ORDER_DICT = {
    'index.md': 1,
    # 'Getting Started' is 2.
    'developer_overview.md': 3,
    'roadmap_design.md': 4,
    'roadmap.md': 5,
    'op_coverage.md': 6,
    'testing_guide.md': 7,

    # Within 'Getting Started' use explicit ordering.
    # Alphabetical would put 'bazel' before 'cmake' and 'python' between 'linux'
    # and 'windows'.
    'getting_started_linux_cmake.md': 1,
    'getting_started_linux_bazel.md': 2,
    'getting_started_linux_vulkan.md': 3,
    'getting_started_windows_cmake.md': 4,
    'getting_started_windows_bazel.md': 5,
    'getting_started_windows_vulkan.md': 6,
    'getting_started_python.md': 7,
    'generic_vulkan_env_setup.md': 8,
}

# A dictionary containing source directory to section tile mappings.
#
# To put a MarkDown file under a certain section, the front matter should
# contain a `parent` field pointing to the section's title. By default we
# use the subdirectory name as the section title. This allows customization.
# Note that the title here must match with index.md file's title under the
# subdirectory.
DIRECTORY_TITLE_DICT = {
    'Dialects': 'Dialect Definitions',
    'GetStarted': 'Getting Started',
}


def process_file(basedir, relpath, filename):
  """Patches the given file in-place with metadata for publication."""

  full_path = os.path.join(basedir, relpath, filename)
  base_name = os.path.splitext(filename)[0]
  with open(full_path, 'r') as f:
    content = f.read()

  # Directly return if the file already has front matter.
  if content.startswith('---\n'):
    return

  front_matter = {}
  # Use the default layout for everything.
  front_matter['layout'] = 'default'
  # Use the base filename as permanent link.
  front_matter['permalink'] = base_name

  # Organize each doc to a section matching its directory structure.
  if relpath and relpath != '.':
    front_matter['parent'] = relpath
    front_matter['permalink'] = f'{relpath}/{front_matter["permalink"]}'

  # Find the title and TOC.
  lines = content.splitlines()
  title_line_index = None
  toc_index = None
  for (index, line) in enumerate(lines):
    if line.startswith('# '):
      title_line_index = index
    if line == '[TOC]':
      toc_index = index
    if title_line_index is not None and toc_index is not None:
      break

  # Replace '[TOC]' with the proper format that can be rendered.
  if toc_index is not None:
    lines[toc_index] = '1. TOC\n{:toc}'

  # Set the title in front matter and disable it to show up in TOC.
  if title_line_index is not None:
    front_matter['title'] = f'"{lines[title_line_index][2:]}"'
    lines.insert(title_line_index + 1, '{: .no_toc }')
  else:
    front_matter['title'] = base_name

  # Override with manually specified metadata if exists.
  if filename in DOC_TITLE_DICT:
    front_matter['title'] = DOC_TITLE_DICT[filename]
  if filename in PERMALINK_DICT:
    front_matter['permalink'] = PERMALINK_DICT[filename]
  if filename in NAVI_ORDER_DICT:
    front_matter['nav_order'] = NAVI_ORDER_DICT[filename]
  if relpath in DIRECTORY_TITLE_DICT:
    front_matter['parent'] = DIRECTORY_TITLE_DICT[relpath]

  # Compose the content prefix for front matter.
  prefix = '\n'.join([f'{k}: {v}' for (k, v) in front_matter.items()])
  prefix = '\n'.join(['---', prefix, '---\n\n'])

  # Compose the new content.
  content = '\n'.join(lines)

  # Substitute specific pattern for callouts to make them prettier.
  content = content.replace('> Tip:<br>\n> &nbsp;&nbsp;&nbsp;&nbsp;',
                            '> Tip\n> {: .label .label-green }\n> ')
  content = content.replace('> Note:<br>\n> &nbsp;&nbsp;&nbsp;&nbsp;',
                            '> Note\n> {: .label .label-blue }\n> ')

  # Update in place.
  with open(full_path, 'w') as f:
    f.write(f'{prefix}{content}')


def process_directory(basedir):
  """Walks the given base directory and processes each MarkDown file."""
  for (dirpath, _, filenames) in os.walk(basedir):
    for filename in filenames:
      if filename.endswith('.md'):
        relpath = os.path.relpath(dirpath, basedir)
        process_file(basedir, relpath, filename)


if __name__ == '__main__':
  args = parse_arguments()
  process_directory(args.base_dir)
