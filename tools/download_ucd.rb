# frozen_string_literal: true

# download-ucd.rb - Download Unicode Character Database (UCD) files for a given version.
#
# Usage: ruby tools/download-ucd.rb <version>
#
# This script downloads the necessary Unicode Character Database (UCD) files for a specified
# Unicode version. It also downloads files related to emoji sequences. The downloaded files
# are saved in the `data/unicode/<version>/` directory.

require 'fileutils'
require 'open-uri'

# File names to be downloaded from the UCD.
FILES = %w[
  Blocks.txt
  CompositionExclusions.txt
  DerivedAge.txt
  DerivedCoreProperties.txt
  DerivedNormalizationProps.txt
  CaseFolding.txt
  PropList.txt
  PropertyAliases.txt
  PropertyValueAliases.txt
  ScriptExtensions.txt
  Scripts.txt
  SpecialCasing.txt
  UnicodeData.txt
  auxiliary/GraphemeBreakProperty.txt
  emoji/emoji-data.txt
  extracted/DerivedCombiningClass.txt
  extracted/DerivedGeneralCategory.txt
  extracted/DerivedName.txt
  extracted/DerivedNumericValues.txt
].freeze

# File names to be downloaded from the `emoji` directory.
EMOJI_FILES = %w[
  emoji-sequences.txt
  emoji-zwj-sequences.txt
].freeze

# Returns the download URL for the given version, type, and file name.
def url_for(version, type, file)
  case type
  when :ucd
    "https://www.unicode.org/Public/#{version}/ucd/#{file}"
  when :emoji
    # `emoji` data files were moved to each version's directory starting from
    # Unicode 17.0.0.
    if version.split('.').first.to_i >= 17
      "https://www.unicode.org/Public/#{version}/emoji/#{file}"
    else
      emoji_version = version.split('.').take(2).join('.')
      "https://www.unicode.org/Public/emoji/#{emoji_version}/#{file}"
    end
  else
    raise ArgumentError, "Unknown type: #{type}"
  end
end

# Returns the local file path for the given version, type, and file name.
def path_for(version, type, file)
  File.join(File.dirname(__FILE__), '..', 'data/unicode', version, type.to_s, file)
end

# Downloads the specified file for the given version and type. If the file already exists
# and is up to date, it will not be downloaded again.
def download(version, type, file)
  url = url_for(version, type, file)
  path = path_for(version, type, file)

  mtime = nil

  FileUtils.mkdir_p(File.dirname(path))

  mtime = File.mtime(path) if File.exist?(path)

  options = {
    # I don't know why but downloading some files under `emoji` directory with
    # `If-Modified-Since` header causes 520 error, so we only set the header
    # for non `emoji` files.
    'If-Modified-Since' => type == :emoji ? nil : mtime&.httpdate,
  }.compact

  begin
    URI.open(url, options) do |remote_file| # rubocop:disable Security/Open
      new_mtime = remote_file.last_modified
      print "Downloading '#{file}'... "
      File.open(path, 'wb') do |local_file|
        IO.copy_stream(remote_file, local_file)
      end
      puts "done (#{File.size(path)} bytes, #{new_mtime})."
      File.utime(new_mtime, new_mtime, path)
    end
  rescue OpenURI::HTTPError => e
    raise e unless e.io.status[0] == '304'

    puts "File '#{file}' is up to date."
  end
end

version = ARGV[0]
if version.nil?
  puts 'Usage: tools/download-ucd.rb <version>'
  exit 1
end

FILES.each do |file|
  download(version, :ucd, file)
end

EMOJI_FILES.each do |file|
  download(version, :emoji, file)
end
