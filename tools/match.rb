# Naraku regex tester — bin/mruby tools/match.rb
#
# Usage (one-shot):
#   bin/mruby tools/match.rb PATTERN SUBJECT
#
# Usage (interactive REPL):
#   bin/mruby tools/match.rb

def show_match(pattern, subject)
  re = Naraku::Regexp.new(pattern)
  md = re.match(subject)

  puts "  pattern : #{pattern}"
  puts "  subject : #{subject}"

  if md.nil?
    puts '  result  : no match'
    return
  end

  whole = md[0]
  b = md.byte_begin(0)
  e = md.byte_end(0)
  puts "  match   : #{whole.inspect}  [#{b}...#{e}]"

  if md.size > 1
    (1...md.size).each do |i|
      cap = md[i]
      puts "  [#{i}]     : #{cap.inspect}"
    end
  end

  puts "  pre     : #{md.pre_match.inspect}"
  puts "  post    : #{md.post_match.inspect}"
rescue Naraku::CompileError => e
  puts "  compile error: #{e.message}"
rescue Naraku::ParseError => e
  puts "  parse error: #{e.message.lines.first.chomp}"
rescue StandardError => e
  puts "  error: #{e.message}"
end

def prompt(msg)
  print msg
  $stdout.flush
  line = $stdin.gets
  return nil if line.nil?

  line.chomp
end

if ARGV.size >= 2
  show_match(ARGV[0], ARGV[1])
elsif ARGV.size == 1
  puts 'Usage: bin/mruby tools/match.rb PATTERN SUBJECT'
  puts '       bin/mruby tools/match.rb          (interactive)'
else
  puts 'Naraku regex tester  (Ctrl-D to quit)'
  puts

  loop do
    pattern = prompt('pattern> ')
    break if pattern.nil? || pattern == 'q' || pattern == 'quit'
    next if pattern.empty?

    subject = prompt('subject> ')
    break if subject.nil?

    puts
    show_match(pattern, subject)
    puts
  end

  puts 'bye.'
end
