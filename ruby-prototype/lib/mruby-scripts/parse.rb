def run(request)
  pattern = request['pattern']
  options = request['options'].transform_keys(&:to_sym)

  warnings = []
  if options[:enable_warning]
    options.delete(:enable_warning)
    options[:warning_func] = -> (warning, offset, length) do
      warnings << {
        'warning_code' => warning,
        'message' => Naraku.warning_message(warning),
        'offset' => offset,
        'length' => length,
      }
    end
  end

  if options[:fold_flags]&.is_a?(Array)
    options[:fold_flags] = options[:fold_flags].map(&:to_sym)
  end

  enc = Naraku::Encoding::UTF_8
  parser = Naraku::Parser.new(enc, pattern, **options)
  node = parser.parse

  parser.postprocess(node) if request['postprocess']

  parser_info = {
    'num_capture_groups' => parser.num_capture_groups,
    'has_named_captures' => parser.has_named_captures,
  }
  if request['postprocess']
    parser_info['capture_entries'] = parser.capture_entries
    parser_info['capture_names_map'] = parser.capture_names_map
  end

  {
    'ok' => true,
    'data' => {
      'node' => node.to_h,
      'parser_info' => parser_info,
      'warnings' => warnings,
    },
  }
rescue Naraku::ParseError => ex
  {
    'ok' => false,
    'error' => {
      'error_code' => ex.error_code,
      'message' => Naraku.error_message(ex.error_code),
      'message_full' => ex.message,
      'offset' => ex.offset,
      'length' => ex.length,
      'class' => 'Naraku::ParseError',
      'warnings' => warnings,
    },
  }
rescue => ex
  {
    'ok' => false,
    'error' => {
      'message' => ex.message,
      'class' => ex.class.to_s,
      'warnings' => warnings,
    },
  }
end

def main(stdin = STDIN, stdout = STDOUT)
  request = JSON.parse(stdin.read)
  response = run(request)
  stdout.write(JSON.generate(response))
end

main
