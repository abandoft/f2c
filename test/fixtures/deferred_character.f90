subroutine deferred_character_matrix(output)
  implicit none
  integer, intent(out) :: output(56)
  character(:), allocatable :: text
  character(:), allocatable :: values(:)
  character(:), allocatable :: copied(:)
  character(:), allocatable :: moved_text(:)
  character(len=3), allocatable :: padded(:)
  character(len=2) :: labels(-1:1)
  integer, allocatable :: owned(:)
  integer, allocatable :: moved(:)
  integer :: numbers(-2:2), short_values(4:5)
  integer :: status, index
  character(len=16) :: message
  character(len=4) :: suffixes

  output = 0

  suffixes = 'BCDE'
  text = 'A'
  do index = 1, 4
    text = text // suffixes(index:index)
  end do
  output(1) = len(text)
  output(2) = ichar(text(1:1))
  output(3) = ichar(text(5:5))

  allocate(character(len=3) :: values(0:2), stat=status)
  output(4) = status
  values(0) = 'X'
  values(1) = 'YZ'
  values(2) = 'LONG'
  output(5) = len(values(0))
  if (values(0) == 'X  ') output(6) = 1
  if (values(2) == 'LON') output(7) = 1

  allocate(character(len=2) :: text, stat=status)
  if (status /= 0) output(8) = 1
  output(9) = len(text)

  deallocate(text, stat=status)
  output(10) = status
  deallocate(text, stat=status)
  if (status /= 0) output(11) = 1

  allocate(character(len=0) :: text, stat=status)
  output(12) = status
  output(13) = len(text)
  text = 'Q'
  output(14) = len(text)

  numbers(-2) = 11
  numbers(-1) = 22
  numbers(0) = 33
  numbers(1) = 44
  numbers(2) = 55
  owned = numbers
  output(15) = owned(-2)
  output(16) = owned(2)

  short_values(4) = 7
  short_values(5) = 9
  owned = short_values
  output(17) = owned(4)
  output(18) = owned(5)

  deallocate(owned)
  allocate(owned, source=numbers)
  output(19) = owned(-2)
  output(20) = owned(2)
  deallocate(owned)

  allocate(owned, mold=short_values)
  owned = 3
  output(21) = owned(4) + owned(5)
  deallocate(owned)

  allocate(owned(7:8), source=4)
  output(22) = owned(7) + owned(8)
  deallocate(owned)

  labels(-1) = 'A'
  labels(0) = 'BC'
  labels(1) = 'DE'
  copied = labels
  output(23) = len(copied(-1))
  if (copied(-1) == 'A ') output(24) = 1
  if (copied(1) == 'DE') output(25) = 1

  copied = values
  if (copied(0) == 'X  ') output(26) = 1
  if (copied(2) == 'LON') output(27) = 1
  output(28) = len(copied(0))
  deallocate(copied)

  allocate(copied, source=labels)
  output(29) = len(copied(-1))
  if (copied(-1) == 'A ') output(30) = 1
  if (copied(1) == 'DE') output(31) = 1
  deallocate(copied)

  allocate(copied, mold=labels)
  copied = 'Z'
  if (copied(-1) == 'Z ' .and. copied(1) == 'Z ') output(32) = 1
  deallocate(copied)

  allocate(copied(4:5), source='Q')
  output(33) = len(copied(4))
  if (copied(4) == 'Q' .and. copied(5) == 'Q') output(34) = 1

  padded = labels
  if (padded(-1) == 'A  ' .and. padded(1) == 'DE ') output(35) = 1
  padded = values
  if (padded(-1) == 'X  ' .and. padded(1) == 'LON') output(36) = 1

  owned = [(index * index, index = -1, 1)]
  output(37) = owned(1) + owned(2) + owned(3)
  owned = [(index, index = 4, 5)]
  output(38) = owned(1) + owned(2)
  deallocate(owned)

  allocate(owned(7:8))
  owned = [9, 10]
  output(39) = owned(7) + owned(8)
  owned = [owned(8), owned(7)]
  output(40) = owned(7) * 10 + owned(8)
  owned = [11, 12, 13]
  output(41) = owned(1) + owned(2) + owned(3)
  status = -1
  message = 'UNCHANGED'
  call move_alloc(owned, moved, stat=status, errmsg=message)
  output(50) = status
  if (message == 'UNCHANGED') output(51) = 1
  output(52) = moved(1) + moved(2) + moved(3)
  owned = [20, 21]
  call move_alloc(from=moved, to=owned)
  output(53) = owned(1) + owned(2) + owned(3)

  deallocate(copied)
  copied = [('AB', index = 1, 2)]
  output(42) = len(copied(1))
  if (copied(1) == 'AB' .and. copied(2) == 'AB') output(43) = 1
  copied = [('XYZ', index = 1, 3)]
  output(44) = len(copied(1))
  if (copied(1) == 'XYZ' .and. copied(3) == 'XYZ') output(45) = 1
  status = 2
  copied = [('K', index = status, 1)]
  copied = ['LM']
  output(46) = len(copied(1))
  if (copied(1) == 'LM') output(47) = 1
  status = -1
  call move_alloc(copied, moved_text, stat=status)
  output(54) = status
  output(55) = len(moved_text(1))
  if (moved_text(1) == 'LM') output(56) = 1

  padded = ['Q  ', 'RS ']
  if (padded(1) == 'Q  ' .and. padded(2) == 'RS ') output(48) = 1
  output(49) = len(padded(1))

  deallocate(owned)
  deallocate(padded)
  deallocate(moved_text)
  deallocate(text)
  deallocate(values)
end subroutine deferred_character_matrix
