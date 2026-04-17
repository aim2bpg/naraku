# frozen_string_literal: true

require 'optparse'

require_relative 'unicode/case_map'

def gen_ascii(case_map)
  print 'static const uint8_t ascii_fold_map[128] = {'
  codes = (0x00..0x7F).to_a
  diff_map = {}
  codes.each_with_index do |code, index|
    if (index % 8).zero?
      puts
      print '// '
      codes[index, 8].each do |c|
        printf ' %6s,', c.chr(Encoding::ASCII_8BIT).inspect
      end
      puts
      print '   '
    end

    fold_result = case_map.fold([code], mode: :simple)
    raise "Unexpected fold result for ASCII code #{code}: #{fold_result.inspect}" if fold_result.length > 1

    fold_code = fold_result.first
    printf '   0x%02X,', fold_code

    diff = code - fold_code
    next if diff.zero?

    diff_map[diff] ||= Unicode::RangeSet.new
    diff_map[diff] << fold_code
  end
  puts
  puts '};'
  puts

  puts 'size_t nk_enc_ascii_get_case_fold('
  puts '    const nk_encoding_t* enc NARAKU_ARG_UNUSED,'
  puts '    nk_fold_flag_t flags NARAKU_ARG_UNUSED,'
  puts '    uint32_t code,'
  puts '    uint32_t* folded_codes'
  puts ') {'
  puts '  if (folded_codes != NULL) {'
  puts '    folded_codes[0] = code <= 0x7F ? ascii_fold_map[code] : code;'
  puts '  }'
  puts '  return 1;'
  puts '}'
  puts

  puts 'size_t nk_enc_ascii_expand_case_unfold('
  puts '    const nk_encoding_t* enc NARAKU_ARG_UNUSED,'
  puts '    nk_fold_flag_t flags NARAKU_ARG_UNUSED,'
  puts '    const uint32_t* folded_codes,'
  puts '    size_t folded_codes_len NARAKU_ARG_UNUSED,'
  puts '    nk_unfold_item_t* unfold_items'
  puts ') {'
  puts '  uint32_t code = folded_codes[0];'

  diff_map.each do |diff, range_set|
    cond = []
    range_set.each_range do |r|
      cond << if r.begin == r.end
                ('code == 0x%02X' % r.begin)
              else
                format('(code >= 0x%02X && code <= 0x%02X)', r.begin, r.end)
              end
    end
    puts "  if (#{cond.join(' || ')}) {"
    puts '    unfold_items[0].folded_codes_len = 1;'
    puts "    unfold_items[0].unfolded_code = (uint32_t)(code #{diff.negative? ? '-' : '+'} #{diff.abs});"
    puts '    return 1;'
    puts '  }'
  end

  puts '  return 0;'
  puts '}'
  puts

  puts 'nk_error_t nk_enc_ascii_iterate_case_fold('
  puts '    const nk_encoding_t* enc NARAKU_ARG_UNUSED,'
  puts '    nk_fold_flag_t flags NARAKU_ARG_UNUSED,'
  puts '    nk_case_fold_callback_t callback,'
  puts '    void* user_data'
  puts ') {'

  diff_map.each do |diff, range_set|
    range_set.each_range do |r|
      puts format('  for (uint32_t code = 0x%02X; code <= 0x%02X; code++) {', r.begin + diff, r.end + diff)
      puts "    uint32_t folded_code = (uint32_t)(code #{diff.negative? ? '+' : '-'} #{diff.abs});"
      puts '    nk_error_t err = callback(code, &folded_code, 1, user_data);'
      puts '    if (err < 0) {'
      puts '      return err;'
      puts '    }'
      puts '  }'
    end
  end

  puts '  return NK_SUCCESS;'
  puts '}'
end

GPERF_COMMAND = %w[gperf -L ANSI-C -c -j1 -i0 -t -C -P -T -E].freeze

def gen_unicode(case_map)
  flags = {
    fold: 'F',
    fold_full: 'FF',
    lower: 'L',
    title: 'T',
    upper: 'U',
  }
  special_flags = {
    fold_full: 'SFF',
    swap: 'SS',
    title: 'ST',
    upper: 'SU',
    upper_title: 'SUT',
  }

  special_array = []
  special_array_size = 0
  unfolds = {}
  puts 'static const struct unicode_fold_item UNICODE_FOLD_ITEMS[] = {'
  fold_items = case_map.fold_items.to_a.sort_by! { |from_code, _item| from_code }
  fold_items.each_with_index do |(from_code, item), index|
    to_codes = item[:to_codes]
    to_types = item[:to_types]
    specials = item[:specials].dup

    unfolds[to_codes.size] ||= []
    unfolds[to_codes.size] << [to_codes, index]

    to_type_flags = to_types.map do |to_type|
      flags[to_type]
    end

    unless specials.empty?
      specials.sort_by! { |s| special_flags[s[:to_type]] }
      to_type_flags << "SI(#{special_array_size})"
      special_item = []
      specials.each do |s|
        special_to_type = s[:to_type]
        special_to_codes = s[:to_codes]
        if special_to_type == :fold_full
          unfolds[special_to_codes.size] ||= []
          unfolds[special_to_codes.size] << [special_to_codes, index]
        end
        to_type_flags << special_flags[special_to_type]
        special_item << [special_to_codes.length, special_to_codes]
        special_array_size += special_to_codes.length
      end
      special_array << special_item
    end

    printf "  {0x%04X, %s, {%s}},\n", from_code, to_type_flags.join('|'), to_codes.map { |c| '0x%04X' % c }.join(', ')
  end
  puts '};'
  puts

  puts 'static const struct unicode_case_map_item UNICODE_CASE_MAP_ITEMS[] = {'
  case_map_items = case_map.case_map_items.to_a.sort_by! { |from_code, _item| from_code }
  case_map_items.each do |from_code, item|
    to_code = item[:to_codes].first
    raise "Unexpected multiple to_codes for case map item with from_code 0x%04X: #{item[:to_codes].inspect}" % from_code if item[:to_codes].length > 1

    specials = item[:specials].dup

    type_flags = item[:to_types].map do |type|
      flags[type]
    end

    unless item[:specials].empty?
      specials.sort_by! { |s| special_flags[s[:to_type]] }
      type_flags << "SI(#{special_array_size})"
      special_item = []
      specials.each do |s|
        special_to_type = s[:to_type]
        special_to_codes = s[:to_codes]
        type_flags << special_flags[special_to_type]
        special_item << [special_to_codes.length, special_to_codes]
        special_array_size += special_to_codes.length
      end
      special_array << special_item
    end

    printf "  {0x%04X, %s, 0x%04X},\n", from_code, type_flags.join('|'), to_code
  end
  puts '};'
  puts

  puts '#define SL(index) (index << UNICODE_SPECIAL_LENGTH_SHIFT)'

  puts 'static const uint32_t UNICODE_SPECIALS[] = {'
  special_array.each do |item|
    print ' '
    item.each do |(length, to_codes)|
      to_codes.each_with_index do |to_code, i|
        if i.zero?
          printf ' SL(%d) | 0x%04X,', length, to_code
        else
          printf ' 0x%04X,', to_code
        end
      end
    end
    puts
  end
  puts '};'
  puts '#undef SL'

  fold_table_source = IO.popen(
    [*GPERF_COMMAND, '-H', 'unicode_fold_table_hash', '-Q', 'unicode_fold_table_pool', '-N', 'unicode_fold_table_lookup', { err: :err }], 'w+'
  ) do |io|
    io.puts '%{'
    io.puts '%}'
    io.puts 'struct unicode_table_entry;'
    io.puts '%%'

    fold_items.each_with_index do |(from_code, _), index|
      from_code_str = 3.times.map { |i| from_code[i * 8, 8] }.reverse.map { |part| '\\x%.2x' % part }.join
      io.puts format('%-20s %d', "\"#{from_code_str}\",", index)
    end
    io.puts '%%'
    io.close_write

    io.read
      .gsub(%r{/\*FALLTHROUGH\*/}, 'NARAKU_FALLTHROUGH;')
      .gsub('{-1}', '{-1, 0}')
      .gsub('offsetof', '(uint16_t)offsetof')
  end

  puts fold_table_source

  case_map_table_source = IO.popen(
    [*GPERF_COMMAND, '-H', 'unicode_case_map_table_hash', '-Q', 'unicode_case_map_table_pool', '-N', 'unicode_case_map_table_lookup', { err: :err }], 'w+'
  ) do |io|
    io.puts '%{'
    io.puts '%}'
    io.puts 'struct unicode_table_entry;'
    io.puts '%%'

    case_map_items.each_with_index do |(from_code, _), index|
      from_code_str = 3.times.map { |i| from_code[i * 8, 8] }.reverse.map { |part| '\\x%.2x' % part }.join
      io.puts format('%-20s %d', "\"#{from_code_str}\",", index)
    end
    io.puts '%%'
    io.close_write

    io.read
      .gsub(%r{/\*FALLTHROUGH\*/}, 'NARAKU_FALLTHROUGH;')
      .gsub('{-1}', '{-1, 0}')
      .gsub('offsetof', '(uint16_t)offsetof')
  end

  puts case_map_table_source

  unfold_index_count_shift = fold_items.size.bit_length
  puts "#define UNICODE_UNFOLD_INDEX_COUNT_SHIFT #{unfold_index_count_shift}"
  puts '#define UNICODE_UNFOLD_INDEX_COUNT_MASK ((1 << UNICODE_UNFOLD_INDEX_COUNT_SHIFT) - 1)'
  puts '#define UNICODE_UNFOLD_INDEX(index) ((index) & UNICODE_UNFOLD_INDEX_COUNT_MASK)'
  puts '#define UNICODE_UNFOLD_INDEX_COUNT(index) ((index) >> UNICODE_UNFOLD_INDEX_COUNT_SHIFT)'
  puts '#define UI(index) (index << UNICODE_UNFOLD_INDEX_COUNT_SHIFT)'

  puts
  puts 'static const size_t UNICODE_UNFOLD_INDICES[] = {'
  unfold_indices = {}
  unfold_index_length = 0
  unfolds.each do |to_codes_len, items|
    unfold_indices[to_codes_len] = []
    indices = items.group_by { |(to_codes, _)| to_codes }.sort
    indices.each do |to_codes, items|
      unfold_indices[to_codes_len] << [to_codes, unfold_index_length]
      fold_item_indices = items.map { |(_, fold_item_index)| fold_item_index }
      unfold_index_length += fold_item_indices.length
      print ' '
      fold_item_indices.each_with_index do |index, i|
        if i.zero?
          printf ' UI(%d) | %d,', fold_item_indices.length, index
        else
          printf ' %d,', index
        end
      end
      puts
    end
  end
  puts '};'
  puts '#undef UI'

  unfold_indices.each do |to_codes_len, items|
    unfold_table_source = IO.popen(
      [*GPERF_COMMAND, '-H', "unicode_unfold#{to_codes_len}_table_hash", '-Q', "unicode_unfold#{to_codes_len}_table_pool", '-N',
       "unicode_unfold#{to_codes_len}_table_lookup", { err: :err }], 'w+'
    ) do |io|
      io.puts '%{'
      io.puts '%}'
      io.puts 'struct unicode_table_entry;'
      io.puts '%%'
      items.each do |(to_codes, unfold_index)|
        to_codes_str = to_codes.flat_map { |to_code| 3.times.map { |i| to_code[i * 8, 8] }.reverse.map { |part| '\\x%.2x' % part } }.join
        io.puts format('%-20s %d', "\"#{to_codes_str}\",", unfold_index)
      end
      io.puts '%%'
      io.close_write

      io.read
        .gsub(%r{/\*FALLTHROUGH\*/}, 'NARAKU_FALLTHROUGH;')
        .gsub('{-1}', '{-1, 0}')
        .gsub('offsetof', '(uint16_t)offsetof')
    end

    puts
    puts unfold_table_source
  end
end

def gen_sb(case_map, enc, prefix)
  codes = (0x80..0xFF).to_a
  valid_range_set = Unicode::RangeSet.new(0x00..0x7F)
  map = {}
  rev_map = {}
  codes.each do |code|
    enc_str = code.chr(enc)
    unicode_code = enc_str.encode(Encoding::UTF_8).ord
    valid_range_set << unicode_code
    map[code] = unicode_code
    rev_map[unicode_code] = code
  rescue StandardError
    next
  end

  diff_map = {}
  print "static const uint8_t #{prefix}_fold_map[128] = {"
  codes.each_with_index do |enc_code, index|
    if (index % 8).zero?
      puts
      print '// '
      codes[index, 8].each do |c|
        c = map[c]&.chr(Encoding::UTF_8)&.inspect || ('"\\x%02X"' % c)
        printf ' %8s,', c
      end
      puts
      print '   '
    end

    begin
      unicode_code = map[enc_code] or raise "Encoding #{enc} code 0x%02X does not map to any Unicode code point" % enc_code
      fold_result = case_map.fold([unicode_code], mode: :simple).pack('U*').encode(enc).codepoints
      raise "Unexpected fold result for code #{unicode_code} (#{enc_code} in encoding #{enc}): #{fold_result.inspect}" if fold_result.length > 1

      fold_code = fold_result.first
    rescue StandardError
      fold_code = enc_code
    end
    printf '     0x%02X,', fold_code

    diff = enc_code - fold_code
    next if diff.zero?

    diff_map[diff] ||= Unicode::RangeSet.new
    diff_map[diff] << fold_code
  end
  puts
  puts '};'
  puts

  has_sharp_s = rev_map.key?(0x00DF)
  puts "size_t #{prefix}_get_case_fold("
  puts '    const nk_encoding_t* enc NARAKU_ARG_UNUSED,'
  puts '    nk_fold_flag_t flags,'
  puts '    uint32_t code,'
  puts '    uint32_t* folded_codes'
  puts ') {'
  puts '  if (code < 0x80) {'
  puts '    return nk_enc_ascii_get_case_fold(enc, flags, code, folded_codes);'
  puts '  }'
  if has_sharp_s
    puts '  if (code == 0x%02X && (flags & NK_FOLD_FULL) != 0) { // LATIN SMALL LETTER SHARP S' % rev_map[0x00DF]
    puts '    if (folded_codes != NULL) {'
    puts '      folded_codes[0] = 0x73;'
    puts '      folded_codes[1] = 0x73;'
    puts '    }'
    puts '    return 2;'
    puts '  }'
  end
  puts '  if (folded_codes != NULL) {'
  puts "    folded_codes[0] = #{prefix}_fold_map[code - 0x80];"
  puts '  }'
  puts '  return 1;'
  puts '}'
  puts

  puts "size_t #{prefix}_expand_case_unfold("
  puts '    const nk_encoding_t* enc NARAKU_ARG_UNUSED,'
  puts '    nk_fold_flag_t flags,'
  puts '    const uint32_t* folded_codes,'
  puts '    size_t folded_codes_len NARAKU_ARG_UNUSED,'
  puts '    nk_unfold_item_t* unfold_items'
  puts ') {'
  puts '  size_t unfold_count = nk_enc_ascii_expand_case_unfold(enc, flags, folded_codes, folded_codes_len, unfold_items);'
  puts '  uint32_t code = folded_codes[0];'

  diff_map.each do |diff, range_set|
    cond = []
    range_set.each_range do |r|
      cond << if r.begin == r.end
                ('code == 0x%02X' % r.begin)
              else
                format('(code >= 0x%02X && code <= 0x%02X)', r.begin, r.end)
              end
    end
    puts "  if (#{cond.join(' || ')}) {"
    puts '    unfold_items[unfold_count].folded_codes_len = 1;'
    puts "    unfold_items[unfold_count].unfolded_code = (uint32_t)(code #{diff.negative? ? '-' : '+'} #{diff.abs});"
    puts '    unfold_count++;'
    puts '  }'
  end

  if has_sharp_s
    puts '  if ((flags & NK_FOLD_FULL) != 0 && code == 0x73 && folded_codes_len >= 2 && folded_codes[1] == 0x73) { // "ss"'
    puts '    unfold_items[unfold_count].folded_codes_len = 2;'
    puts '    unfold_items[unfold_count].unfolded_code = 0x%02X;' % rev_map[0x00DF]
    puts '    unfold_count++;'
    puts '  }'
  end

  puts '  return unfold_count;'
  puts '}'
  puts

  puts "nk_error_t #{prefix}_iterate_case_fold("
  puts '    const nk_encoding_t* enc NARAKU_ARG_UNUSED,'
  puts '    nk_fold_flag_t flags,'
  puts '    nk_case_fold_callback_t callback,'
  puts '    void* user_data'
  puts ') {'
  puts '  nk_error_t err = nk_enc_ascii_iterate_case_fold(enc, flags, callback, user_data);'
  puts '  if (err < 0) {'
  puts '    return err;'
  puts '  }'

  diff_map.each do |diff, range_set|
    range_set.each_range do |r|
      puts format('  for (uint32_t code = 0x%02X; code <= 0x%02X; code++) {', r.begin + diff, r.end + diff)
      puts "    uint32_t folded_code = (uint32_t)(code #{diff.negative? ? '+' : '-'} #{diff.abs});"
      puts '    nk_error_t err = callback(code, &folded_code, 1, user_data);'
      puts '    if (err < 0) {'
      puts '      return err;'
      puts '    }'
      puts '  }'
    end
  end

  if has_sharp_s
    puts '  if ((flags & NK_FOLD_FULL) != 0) {'
    puts '    uint32_t folded_codes[2] = {0x73, 0x73}; // "ss"'
    puts '    nk_error_t err = callback(0x%02X, folded_codes, 2, user_data); // LATIN SMALL LETTER SHARP S' % rev_map[0x00DF]
    puts '    if (err < 0) {'
    puts '      return err;'
    puts '    }'
    puts '  }'
  end

  puts '  return NK_SUCCESS;'
  puts '}'
end

def gen_mb2(case_map, enc, prefix)
  codes = (0x80..0xFFFF).to_a
  valid_range_set = Unicode::RangeSet.new(0x00..0x7F)
  map = {}
  rev_map = {}
  codes.each do |code|
    enc_str = code.chr(enc)
    unicode_code = enc_str.encode(Encoding::UTF_8).ord
    valid_range_set << unicode_code
    map[code] = unicode_code
    rev_map[unicode_code] = code
  rescue StandardError
    next
  end

  diff_map = {}
  codes.each_with_index do |enc_code, _index|
    begin
      unicode_code = map[enc_code] or raise "Encoding #{enc} code 0x%02X does not map to any Unicode code point" % enc_code
      fold_result = case_map.fold([unicode_code], mode: :simple).pack('U*').encode(enc).codepoints
      raise "Unexpected fold result for code #{unicode_code} (#{enc_code} in encoding #{enc}): #{fold_result.inspect}" if fold_result.length > 1

      fold_code = fold_result.first
    rescue StandardError
      fold_code = enc_code
    end

    diff = enc_code - fold_code
    next if diff.zero?

    diff_map[diff] ||= Unicode::RangeSet.new
    diff_map[diff] << fold_code
  end

  has_sharp_s = rev_map.key?(0x00DF)

  puts "size_t #{prefix}_get_case_fold("
  puts '    const nk_encoding_t* enc NARAKU_ARG_UNUSED,'
  puts '    nk_fold_flag_t flags,'
  puts '    uint32_t code,'
  puts '    uint32_t* folded_codes'
  puts ') {'
  puts '  if (code < 0x80) {'
  puts '    return nk_enc_ascii_get_case_fold(enc, flags, code, folded_codes);'
  puts '  }'
  if has_sharp_s
    puts '  if (code == 0x%02X && (flags & NK_FOLD_FULL) != 0) { // LATIN SMALL LETTER SHARP S' % rev_map[0x00DF]
    puts '    if (folded_codes != NULL) {'
    puts '      folded_codes[0] = 0x73;'
    puts '      folded_codes[1] = 0x73;'
    puts '    }'
    puts '    return 2;'
    puts '  }'
  end
  diff_map.each do |diff, range_set|
    cond = []
    range_set.each_range do |r|
      cond << if r.begin == r.end
                format('code == 0x%02X', r.begin + diff)
              else
                format('code >= 0x%02X && code <= 0x%02X', r.begin + diff, r.end + diff)
              end
    end
    puts "  if (#{cond.join(' || ')}) {"
    puts '    if (folded_codes != NULL) {'
    puts "      folded_codes[0] = (uint32_t)(code #{diff.negative? ? '+' : '-'} #{diff.abs});"
    puts '    }'
    puts '    return 1;'
    puts '  }'
  end
  puts '  if (folded_codes != NULL) {'
  puts '    folded_codes[0] = code;'
  puts '  }'
  puts '  return 1;'
  puts '}'
  puts

  puts "size_t #{prefix}_expand_case_unfold("
  puts '    const nk_encoding_t* enc NARAKU_ARG_UNUSED,'
  puts '    nk_fold_flag_t flags,'
  puts '    const uint32_t* folded_codes,'
  puts '    size_t folded_codes_len NARAKU_ARG_UNUSED,'
  puts '    nk_unfold_item_t* unfold_items'
  puts ') {'
  puts '  size_t unfold_count = nk_enc_ascii_expand_case_unfold(enc, flags, folded_codes, folded_codes_len, unfold_items);'
  puts '  uint32_t code = folded_codes[0];'

  diff_map.each do |diff, range_set|
    cond = []
    range_set.each_range do |r|
      cond << if r.begin == r.end
                format('code == 0x%02X', r.begin + diff)
              else
                format('code >= 0x%02X && code <= 0x%02X', r.begin, r.end)
              end
    end

    puts "  if (#{cond.join(' || ')}) {"
    puts '    unfold_items[unfold_count].folded_codes_len = 1;'
    puts "    unfold_items[unfold_count].unfolded_code = (uint32_t)(code #{diff.negative? ? '-' : '+'} #{diff.abs});"
    puts '    unfold_count++;'
    puts '  }'
  end

  if has_sharp_s
    puts '  if ((flags & NK_FOLD_FULL) != 0 && code == 0x73 && folded_codes_len >= 2 && folded_codes[1] == 0x73) { // "ss"'
    puts '    unfold_items[unfold_count].folded_codes_len = 2;'
    puts '    unfold_items[unfold_count].unfolded_code = 0x%02X;' % rev_map[0x00DF]
    puts '    unfold_count++;'
    puts '  }'
  end
  puts '  return unfold_count;'
  puts '}'
  puts

  puts "nk_error_t #{prefix}_iterate_case_fold("
  puts '    const nk_encoding_t* enc NARAKU_ARG_UNUSED,'
  puts '    nk_fold_flag_t flags,'
  puts '    nk_case_fold_callback_t callback,'
  puts '    void* user_data'
  puts ') {'
  puts '  nk_error_t err = nk_enc_ascii_iterate_case_fold(enc, flags, callback, user_data);'
  puts '  if (err < 0) {'
  puts '    return err;'
  puts '  }'

  diff_map.each do |diff, range_set|
    range_set.each_range do |r|
      puts format('  for (uint32_t code = 0x%02X; code <= 0x%02X; code++) {', r.begin + diff, r.end + diff)
      puts "    uint32_t folded_code = (uint32_t)(code #{diff.negative? ? '+' : '-'} #{diff.abs});"
      puts '    nk_error_t err = callback(code, &folded_code, 1, user_data);'
      puts '    if (err < 0) {'
      puts '      return err;'
      puts '    }'
      puts '  }'
    end
  end

  if has_sharp_s
    puts '  if ((flags & NK_FOLD_FULL) != 0) {'
    puts '    uint32_t folded_codes[2] = {0x73, 0x73}; // "ss"'
    puts '    nk_error_t err = callback(0x%02X, folded_codes, 2, user_data); // LATIN SMALL LETTER SHARP S' % rev_map[0x00DF]
    puts '    if (err < 0) {'
    puts '      return err;'
    puts '    }'
    puts '  }'
  end

  puts '  return NK_SUCCESS;'
  puts '}'
end

opt = OptionParser.new

mode = nil
enc_name = nil
prefix = nil
opt.on('--ascii', 'Generate ASCII case map') { mode = :ascii }
opt.on('--unicode', 'Generate Unicode case map') { mode = :unicode }
opt.on('--single-byte ENC', 'Generate case map for single-byte encoding ENC') do |en|
  mode = :single_byte
  enc_name = en
end
opt.on('--multi-byte2 ENC', 'Generate case map for multi-byte encoding ENC with 2 bytes max') do |en|
  mode = :multi_byte2
  enc_name = en
end
opt.on('--prefix PREFIX', 'Prefix for generated function names') do |p|
  prefix = p
end

opt.parse!(ARGV)

version = ARGV[0]
if version.nil?
  puts opt
  exit 1
end

puts '// This file is generated by `tools/gen-case-map.rb`. DO NOT EDIT!'
puts

case_map = Unicode::CaseMap.new(version)
case mode
when :ascii
  gen_ascii(case_map)
when :unicode
  gen_unicode(case_map)
when :single_byte
  enc = Encoding.find(enc_name)
  gen_sb(case_map, enc, prefix)
when :multi_byte2
  enc = Encoding.find(enc_name)
  gen_mb2(case_map, enc, prefix)
end
