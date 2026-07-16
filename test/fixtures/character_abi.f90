subroutine assign_assumed(destination, source)
  character*( * ) destination
  character*( * ) source
  destination = source
end subroutine assign_assumed

subroutine assign_fixed(destination, source)
  character*4 destination
  character*(*) source
  destination = source
end subroutine assign_fixed

subroutine shift_overlap(value)
  character*(*) value
  value(2:5) = value(1:4)
end subroutine shift_overlap

subroutine pad_substring(value)
  character*(*) value
  value(2:5) = 'Q'
end subroutine pad_substring

subroutine compare_character(left, right, result)
  character*(*) left
  character*(*) right
  integer result
  result = 0
  if (left == right) result = result + 1
  if (left /= right) result = result + 2
  if (left < right) result = result + 4
  if (left <= right) result = result + 8
  if (left > right) result = result + 16
  if (left >= right) result = result + 32
end subroutine compare_character

character*4 function make_character_result(selector)
  integer selector
  if (selector .eq. 1) then
    make_character_result = 'A'
  else
    make_character_result = 'WXYZ'
  end if
end function make_character_result

subroutine copy_character_result(destination, selector)
  character*4 destination
  character*4 make_character_result
  external make_character_result
  integer selector
  destination = make_character_result(selector)
end subroutine copy_character_result

subroutine fill_character_array(values)
  character*4 values(2)
  values(1) = 'A'
  values(2) = 'WXYZ'
end subroutine fill_character_array

subroutine copy_local_character_array(destination)
  character*4 destination
  character*4 values(2)
  values(1) = 'LEFT'
  values(2) = 'R'
  destination = values(2)
end subroutine copy_local_character_array

subroutine concatenate_character(destination, left, right)
  character*6 destination
  character*(*) left
  character*(*) right
  destination = left // right
end subroutine concatenate_character

subroutine copy_character_data(output)
  character*4 output(3)
  character*4 values(3)
  data values / 'A', 'BC', 'WXYZQ' /
  output(1) = values(1)
  output(2) = values(2)
  output(3) = values(3)
end subroutine copy_character_data

subroutine character_sections(values, source)
  character*4 values(4)
  character*2 source(4)
  values(2:4) = values(1:3)
  values(1:4:2) = source(4:1:-2)
  values(2:4:2) = 'Z'
end subroutine character_sections

subroutine character_whole(values, source)
  character*4 values(3)
  character*2 source(3)
  values = source
end subroutine character_whole

subroutine character_broadcast(values)
  character*4 values(3)
  values = 'K'
end subroutine character_broadcast

subroutine character_constructor(values)
  character*4 values(3)
  values = [ 'A   ', 'BC  ', 'WXYZ' ]
end subroutine character_constructor

subroutine declaration_character(output)
  character*4 output(4)
  integer, parameter :: base = 3
  character(len=base+1) values(3) = (/ 'A   ', 'BC  ', 'WXYZ' /)
  character(len=base+1) scalar = 'Q'
  output(1:3) = values
  output(4) = scalar
  values(1) = 'MUT'
  scalar = 'R'
end subroutine declaration_character

subroutine character_lengths(value, declared, trimmed)
  character*(*) value
  integer declared
  integer trimmed
  declared = len(value)
  trimmed = len_trim(value)
end subroutine character_lengths

subroutine character_length_spec(output)
  character*4 output
  integer, parameter :: base = 3
  character(len=base+1) local
  local = 'X'
  output = local
end subroutine character_length_spec

subroutine automatic_character(value, output, declared)
  character*(*) value
  character*(*) output
  integer declared
  character(len=len(value)+2) local
  local = value // 'XY'
  output = local
  declared = len(local)
  if (declared .gt. 0) return
  output = 'BAD'
end subroutine automatic_character
