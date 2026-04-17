def normalize_fold_flags(raw_fold_flags)
  return [] if raw_fold_flags.nil?

  raw_fold_flags.map(&:to_sym)
end

def normalize_cprop(raw_cprop)
  return raw_cprop.to_i if raw_cprop.is_a?(Integer)

  raw_cprop.to_s
end

def run(request)
  enc = Naraku::Encoding::UTF_8
  fold_flags = normalize_fold_flags(request['fold_flags'])

  result = case request['method']
           when 'case_fold'
             enc.case_fold(request['code'], *fold_flags)
           when 'expand_case_unfold'
             enc.expand_case_unfold(request['codes'], *fold_flags)
           when 'iterate_case_fold'
             items = []
             enc.iterate_case_fold(*fold_flags) do |code, folded_codes|
               items << {
                 'code' => code,
                 'folded_codes' => folded_codes,
               }
             end
             items
           when 'cprop_code_range'
             enc.cprop_code_range(normalize_cprop(request['cprop'])).map do |range|
               {
                 'begin' => range.begin,
                 'end' => range.end,
               }
             end
           else
             raise ArgumentError, "unknown method: #{request['method'].inspect}"
           end

  {
    'ok' => true,
    'data' => result,
  }
rescue => ex
  {
    'ok' => false,
    'error' => {
      'message' => ex.message,
      'class' => ex.class.to_s,
    },
  }
end

def main(stdin = STDIN, stdout = STDOUT)
  request = JSON.parse(stdin.read)
  response = run(request)
  stdout.write(JSON.generate(response))
end

main
