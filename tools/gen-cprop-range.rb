# frozen_string_literal: true

require 'optparse'

require_relative 'unicode/cprop_catalog'

def gen_ascii(cat)
  codes = (0x00..0x7F).to_a
  print 'static uint16_t ascii_cprop_bits[128] = {'
  codes.each_with_index do |code, index|
    if (index % 8).zero?
      puts
      print '// '
      codes[index, 8].each do |c|
        printf ' %8s,', c.chr(Encoding::UTF_8).inspect
      end
      puts
      print '    '
    else
      print ' '
    end

    bits = 0
    cat.cprops[0...16].each_with_index do |cprop, i|
      bits |= (1 << i) if cprop.range_set.include?(code)
    end

    printf '  0x%04X,', bits
  end
  puts
  puts '};'
  puts

  puts 'bool nk_enc_ascii_code_is_cprop('
  puts '    const nk_encoding_t* enc ARG_UNUSED,'
  puts '    uint32_t code,'
  puts '    nk_cprop_t cprop'
  puts ') {'
  puts '  if (cprop < 16) {'
  puts '    return ascii_cprop_bits[code] & (1 << cprop);'
  puts '  }'
  puts '  switch (cprop) {'
  puts '    case NK_CPROP_ANY:'
  puts '    case NK_CPROP_ASSIGNED:'
  puts '      return code <= 0x7F;'

  cat.cprops[(16 + 2)..].each do |cprop|
    break unless cprop.prop_name == 'Script'

    range_set = cprop.range_set & Unicode::RangeSet.new(0x00..0x7F)
    next if range_set.empty?

    puts "    case NK_CPROP_#{cprop.constant_name}:"
    range_set.each_range do |r|
      if r.begin == r.end
        puts "      if (code == 0x#{r.begin.to_s(16).upcase}) return true;"
      else
        puts "      if (code >= 0x#{r.begin.to_s(16).upcase} && code <= 0x#{r.end.to_s(16).upcase}) return true;"
      end
    end
    puts '      break;'
  end

  puts '  }'
  puts '  return false;'
  puts '}'
end

def gen_unicode(cat)
  written = {}
  cat.cprops.each do |cprop|
    if written.key?(cprop.range_set)
      puts "#define UNICODE_CPROP_#{cprop.constant_name}_RANGE UNICODE_CPROP_#{written[cprop.range_set].constant_name}_RANGE"
      puts
      next
    end
    written[cprop.range_set] = cprop

    puts "static const uint32_t UNICODE_CPROP_#{cprop.constant_name}_RANGE[] = {"
    puts "  #{cprop.range_set.range_size},"
    cprop.range_set.each_range do |r|
      puts "  0x#{r.begin.to_s(16).upcase}, 0x#{r.end.to_s(16).upcase},"
    end
    puts '};'
    puts
  end

  puts 'static const uint32_t* UNICODE_CPROP_RANGES[] = {'
  cat.cprops.each do |cprop|
    puts "  UNICODE_CPROP_#{cprop.constant_name}_RANGE,"
  end
  puts '};'
end

def conv_range(range_set, rev_map)
  conv_range_set = Unicode::RangeSet.new
  range_set.each_range do |r|
    r.each do |unicode_code|
      conv_range_set << (rev_map[unicode_code] || unicode_code)
    end
  end
  conv_range_set
end

def gen_sb(cat, enc, prefix)
  codes = (0x80..0xFF).to_a
  valid_range_set = Unicode::RangeSet.new(0x00..0x7F)
  map = {}
  rev_map = {}
  codes.each_with_index do |code, _index|
    enc_code = code.chr(enc)
    unicode_code = enc_code.encode(Encoding::UTF_8).ord
    valid_range_set << unicode_code
    map[code] = unicode_code
    rev_map[unicode_code] = code
  rescue StandardError
    next
  end

  print "static uint16_t #{prefix}_cprop_bits[128] = {"
  codes.each_with_index do |code, index|
    unicode_code = map[code]

    if (index % 8).zero?
      puts
      print '// '
      codes[index, 8].each do |c|
        c = map[c]&.chr(Encoding::UTF_8)&.inspect || ('"\\x%02X"' % c)
        printf ' %8s,', c
      end
      puts
      print '    '
    else
      print ' '
    end

    bits = 0
    if unicode_code
      cat.cprops[0...16].each_with_index do |cprop, i|
        bits |= (1 << i) if cprop.range_set.include?(unicode_code)
      end
    end

    printf '  0x%04X,', bits
  end
  puts
  puts '};'
  puts

  puts "bool #{prefix}_code_is_cprop("
  puts '    const nk_encoding_t* enc ARG_UNUSED,'
  puts '    uint32_t code,'
  puts '    nk_cprop_t cprop'
  puts ') {'
  puts '  if (code < 0x80) {'
  puts '    return nk_enc_ascii_code_is_cprop(enc, code, cprop);'
  puts '  } else if (cprop < 16) {'
  puts "    return #{prefix}_cprop_bits[code - 0x80] & (1 << cprop);"
  puts '  }'
  puts '  switch (cprop) {'
  puts '  case NK_CPROP_ANY:'
  puts '    return code <= 0xFF;'
  puts '  case NK_CPROP_ASSIGNED:'
  conv_range(valid_range_set, rev_map).each_range do |r|
    if r.begin == r.end
      puts "    if (code == 0x#{r.begin.to_s(16).upcase}) return true;"
    else
      puts "    if (code >= 0x#{r.begin.to_s(16).upcase} && code <= 0x#{r.end.to_s(16).upcase}) return true;"
    end
  end
  puts '    break;'

  cat.cprops[(16 + 2)..].each do |cprop|
    break unless cprop.prop_name == 'Script'

    range_set = cprop.range_set & valid_range_set
    next if range_set.empty?

    puts "  case NK_CPROP_#{cprop.constant_name}:"
    conv_range(range_set, rev_map).each_range do |r|
      if r.begin == r.end
        puts "    if (code == 0x#{r.begin.to_s(16).upcase}) return true;"
      else
        puts "    if (code >= 0x#{r.begin.to_s(16).upcase} && code <= 0x#{r.end.to_s(16).upcase}) return true;"
      end
    end
    puts '    break;'
  end

  puts '  }'
  puts '  return false;'
  puts '}'
end

def gen_mb2(cat, enc, prefix)
  codes = (0x80..0xFFFF).to_a
  valid_range_set = Unicode::RangeSet.new(0x00..0x7F)
  map = {}
  rev_map = {}
  codes.each_with_index do |code, _index|
    enc_code = code.chr(enc)
    unicode_code = enc_code.encode(Encoding::UTF_8).ord
    valid_range_set << unicode_code
    map[code] = unicode_code
    rev_map[unicode_code] = code
  rescue StandardError
    next
  end

  valid_cprops = []
  written = {}
  cat.cprops[0..cat.max_default_support_cprop_id].each do |cprop|
    next if cprop.prop_name == 'ASCII'

    range_set = cprop.range_set & valid_range_set
    next if range_set.empty?

    valid_cprops << cprop
    conv_range_set = conv_range(range_set, rev_map)
    if written[conv_range_set]
      existing_cprop = written[conv_range_set]
      puts "#define #{prefix.upcase}_CPROP_#{cprop.constant_name}_RANGE #{prefix.upcase}_CPROP_#{existing_cprop.constant_name}_RANGE"
    else
      written[conv_range_set] = cprop
      puts "static const uint32_t #{prefix.upcase}_CPROP_#{cprop.constant_name}_RANGE[] = {"
      puts "  #{conv_range_set.range_size},"
      conv_range_set.each_range do |r|
        puts "  0x#{r.begin.to_s(16).upcase}, 0x#{r.end.to_s(16).upcase},"
      end
      puts '};'
    end
    puts
  end

  puts "bool #{prefix}_code_is_cprop("
  puts '    const nk_encoding_t* enc ARG_UNUSED,'
  puts '    uint32_t code,'
  puts '    nk_cprop_t cprop'
  puts ') {'
  puts '  if (code < 0x80) {'
  puts '    return nk_enc_ascii_code_is_cprop(enc, code, cprop);'
  puts '  }'
  puts '  switch (cprop) {'
  valid_cprops.each do |cprop|
    puts "  case NK_CPROP_#{cprop.constant_name}:"
    puts '    {'
    puts "      size_t len = #{prefix.upcase}_CPROP_#{cprop.constant_name}_RANGE[0];"
    puts "      const uint32_t* intervals = &#{prefix.upcase}_CPROP_#{cprop.constant_name}_RANGE[1];"
    puts '      if (code_in_code_range(code, len, intervals)) {'
    puts '        return true;'
    puts '      }'
    puts '    }'
    puts '    break;'
  end
  puts '  }'
  puts '  return false;'
  puts '}'
  puts

  puts "nk_error_t #{prefix}_get_cprop_code_range("
  puts '    const nk_encoding_t* enc ARG_UNUSED,'
  puts '    nk_cprop_t cprop,'
  puts '    nk_code_range_delegation_t* out_delegation,'
  puts '    nk_static_code_range_t* out_code_range'
  puts ') {'
  puts '  switch (cprop) {'
  valid_cprops.each do |cprop|
    puts "  case NK_CPROP_#{cprop.constant_name}:"
    puts "    out_code_range->len = #{prefix.upcase}_CPROP_#{cprop.constant_name}_RANGE[0];"
    puts "    out_code_range->intervals = &#{prefix.upcase}_CPROP_#{cprop.constant_name}_RANGE[1];"
    puts '    *out_delegation = NK_ENC_NO_DELEGATION;'
    puts '    return NK_SUCCESS;'
  end
  puts '  }'
  puts '  if (cprop > NK_MAX_DEFAULT_SUPPORT_CPROP) {'
  puts '    return NK_ERR_UNSUPPORTED_CHAR_PROPERTY;'
  puts '  }'
  puts '  out_code_range->len = 0;'
  puts '  out_code_range->intervals = NULL;'
  puts '  *out_delegation = NK_ENC_NO_DELEGATION;'
  puts '  return NK_SUCCESS;'
  puts '}'
end

def gen_mb_full(cat, enc, prefix)
  unicode_codes = (0x80..0x10FFFF).to_a
  valid_range_set = Unicode::RangeSet.new(0x00..0x7F)
  map = {}
  rev_map = {}
  unicode_codes.each_with_index do |unicode_code, _index|
    enc_code = unicode_code.chr(Encoding::UTF_8).encode(enc).ord
    valid_range_set << unicode_code
    map[enc_code] = unicode_code
    rev_map[unicode_code] = enc_code
  rescue StandardError
    next
  end

  valid_cprops = []
  written = {}
  cat.cprops[0..cat.max_default_support_cprop_id].each do |cprop|
    next if cprop.prop_name == 'ASCII'

    range_set = cprop.range_set & valid_range_set
    next if range_set.empty?

    valid_cprops << cprop
    conv_range_set = conv_range(range_set, rev_map)
    if written[conv_range_set]
      existing_cprop = written[conv_range_set]
      puts "#define #{prefix.upcase}_CPROP_#{cprop.constant_name}_RANGE #{prefix.upcase}_CPROP_#{existing_cprop.constant_name}_RANGE"
    else
      written[conv_range_set] = cprop
      puts "static const uint32_t #{prefix.upcase}_CPROP_#{cprop.constant_name}_RANGE[] = {"
      puts "  #{conv_range_set.range_size},"
      conv_range_set.each_range do |r|
        puts "  0x#{r.begin.to_s(16).upcase}, 0x#{r.end.to_s(16).upcase},"
      end
      puts '};'
    end
    puts
  end

  puts "bool #{prefix}_code_is_cprop("
  puts '    const nk_encoding_t* enc ARG_UNUSED,'
  puts '    uint32_t code,'
  puts '    nk_cprop_t cprop'
  puts ') {'
  puts '  if (code < 0x80) {'
  puts '    return nk_enc_ascii_code_is_cprop(enc, code, cprop);'
  puts '  }'
  puts '  switch (cprop) {'
  valid_cprops.each do |cprop|
    puts "  case NK_CPROP_#{cprop.constant_name}:"
    puts '    {'
    puts "      size_t len = #{prefix.upcase}_CPROP_#{cprop.constant_name}_RANGE[0];"
    puts "      const uint32_t* intervals = &#{prefix.upcase}_CPROP_#{cprop.constant_name}_RANGE[1];"
    puts '      if (code_in_code_range(code, len, intervals)) {'
    puts '        return true;'
    puts '      }'
    puts '    }'
    puts '    break;'
  end
  puts '  }'
  puts '  return false;'
  puts '}'
  puts

  puts "nk_error_t #{prefix}_get_cprop_code_range("
  puts '    const nk_encoding_t* enc ARG_UNUSED,'
  puts '    nk_cprop_t cprop,'
  puts '    nk_code_range_delegation_t* out_delegation,'
  puts '    nk_static_code_range_t* out_code_range,'
  puts ') {'
  puts '  switch (cprop) {'
  valid_cprops.each do |cprop|
    puts "  case NK_CPROP_#{cprop.constant_name}:"
    puts "    out_code_range->len = #{prefix.upcase}_CPROP_#{cprop.constant_name}_RANGE[0];"
    puts "    out_code_range->intervals = &#{prefix.upcase}_CPROP_#{cprop.constant_name}_RANGE[1];"
    puts '    *out_delegation = NK_ENC_NO_DELEGATION;'
    puts '    return NK_SUCCESS;'
  end
  puts '  }'
  puts '  if (cprop > NK_MAX_DEFAULT_SUPPORT_CPROP) {'
  puts '    return NK_ERR_UNSUPPORTED_CHAR_PROPERTY;'
  puts '  }'
  puts '  out_code_range->len = 0;'
  puts '  out_code_range->intervals = NULL;'
  puts '  *out_delegation = NK_ENC_NO_DELEGATION;'
  puts '  return NK_SUCCESS;'
  puts '}'
end

opt = OptionParser.new

mode = nil
enc_name = nil
prefix = nil

opt.on('--ascii', 'Generate cprop bits for ASCII (0x00..0x7F)') { mode = :ascii }
opt.on('--unicode', 'Generate cprop bits for Unicode') { mode = :unicode }
opt.on('--single-byte ENC_NAME', 'Generate cprop bits for a single byte encoding') do
  mode = :single_byte
  enc_name = _1
end
opt.on('--multi-byte2 ENC_NAME', 'Generate cprop bits for a multi-byte encoding (for max 2 bytes encoding; e.g., Shift_JIS)') do
  mode = :multi_byte2
  enc_name = _1
end
opt.on('--multi-byte-full ENC_NAME', 'Generate cprop bits for a multi-byte encoding (for max 4 bytes encoding; e.g., GB18030)') do
  mode = :multi_byte_full
  enc_name = _1
end
opt.on('--prefix PREFIX', 'Prefix for the generated variable names') do
  prefix = _1
end

opt.parse!(ARGV)
version = ARGV[0]
if version.nil?
  puts opt
  exit 1
end

puts '// This file is generated by `tools/gen-cprop-range.rb`. DO NOT EDIT!'
puts

cat = Unicode::CpropCatalog.new(version)

case mode
when :ascii
  gen_ascii(cat)
when :unicode
  gen_unicode(cat)
when :single_byte
  enc = Encoding.find(enc_name)
  gen_sb(cat, enc, prefix)
when :multi_byte2
  enc = Encoding.find(enc_name)
  gen_mb2(cat, enc, prefix)
when :multi_byte_full
  enc = Encoding.find(enc_name)
  gen_mb_full(cat, enc, prefix)
end
