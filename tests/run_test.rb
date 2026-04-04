# frozen_string_literal: true

# This file is interpreted by MRuby.

# First, we need to define methods that are not available in MRuby.

REQUIRE_BASE_DIR = File.expand_path('../', __FILE__)

def require(path)
  absolute_path = File.expand_path(path, REQUIRE_BASE_DIR)
  absolute_path += '.rb' unless absolute_path.end_with?('.rb')

  eval File.read(absolute_path), nil, absolute_path, 1
end

class Array
  def splice(index, length, *replacement)
    removed = self[index, length]
    self[index, length] = replacement
    removed
  end
end

require 'mtest/mtest'

require 'encoding/test_propname_to_ctype'
require 'encoding/test_ascii_8bit'
require 'encoding/test_iso_8859_1'
require 'encoding/test_shift_jis'
require 'encoding/test_us_ascii'
require 'encoding/test_utf_8'

Mtest.run(__FILE__, ARGV)
