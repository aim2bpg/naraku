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

  options[:fold] = options[:fold].map(&:to_sym) if options[:fold]&.is_a?(Array)

  enc = Naraku::Encoding::UTF_8
  parser = Naraku::Parser.new(enc, pattern, **options)
  node = parser.parse

  {
    'ok' => true,
    'data' => {
      'node' => node.to_h,
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
