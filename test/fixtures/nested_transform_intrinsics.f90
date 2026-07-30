program nested_transform_intrinsics
  implicit none

  integer :: source(6), matrix(2, 3), nested_matrix(2, 2)
  integer :: elemental_matrix(2, 2), shifted_matrix(2, 3), ended_matrix(2, 3)
  integer :: packed_shift(3), unpacked(6), spread_matrix(3, 2)
  integer :: nested_sum, evaluation_count
  integer, allocatable :: dynamic_matrix(:, :)
  logical :: mask(6)
  character(len=3) :: words(4), nested_words(2)
  logical :: word_mask(4)

  source = (/1, 2, 3, 4, 5, 6/)
  mask = (/.true., .false., .true., .false., .true., .false./)
  matrix = reshape(source, (/2, 3/))

  nested_matrix = reshape(pack(source, source > 2), (/2, 2/))
  if (any(nested_matrix /= reshape((/3, 4, 5, 6/), (/2, 2/)))) stop 1

  elemental_matrix = reshape(pack(source, source > 2), (/2, 2/)) + 10
  if (any(elemental_matrix /= reshape((/13, 14, 15, 16/), (/2, 2/)))) stop 2

  packed_shift = pack(cshift(source, 1), cshift(mask, 1))
  if (any(packed_shift /= (/3, 5, 1/))) stop 3

  unpacked = unpack(pack(source, mask), mask, eoshift(source, 1, boundary=-1))
  if (any(unpacked /= (/1, 3, 3, 5, 5, -1/))) stop 4

  spread_matrix = spread(pack(source, mask), 2, 2)
  if (any(spread_matrix /= reshape((/1, 3, 5, 1, 3, 5/), (/3, 2/)))) stop 5

  shifted_matrix = cshift(reshape(source, (/2, 3/)), 1, dim=1)
  if (any(shifted_matrix /= reshape((/2, 1, 4, 3, 6, 5/), (/2, 3/)))) stop 6

  ended_matrix = eoshift(reshape(source, (/2, 3/)), 1, boundary=-1, dim=2)
  if (any(ended_matrix /= reshape((/3, 4, 5, 6, -1, -1/), (/2, 3/)))) stop 7

  nested_sum = sum(reshape(pack(source, mask), (/3/)))
  if (nested_sum /= 9) stop 8

  allocate(dynamic_matrix(1, 1))
  dynamic_matrix = spread(pack(source, mask), 2, 0)
  if (size(dynamic_matrix, 1) /= 3 .or. size(dynamic_matrix, 2) /= 0) stop 9

  call check_matrix(reshape(pack(source, source > 2), (/2, 2/)))

  evaluation_count = 0
  shifted_matrix = reshape(cshift(source, next_shift()), (/2, 3/))
  if (evaluation_count /= 1) stop 11
  if (any(shifted_matrix /= reshape((/2, 3, 4, 5, 6, 1/), (/2, 3/)))) stop 12

  words = (/'aa ', 'b  ', 'ccc', 'dd '/)
  word_mask = (/.true., .false., .true., .false./)
  nested_words = pack(cshift(words, 1), cshift(word_mask, 1))
  if (nested_words(1) /= 'ccc' .or. nested_words(2) /= 'aa ') stop 13

contains

  subroutine check_matrix(value)
    integer, intent(in) :: value(:, :)

    if (size(value, 1) /= 2 .or. size(value, 2) /= 2) stop 10
    if (any(value /= reshape((/3, 4, 5, 6/), (/2, 2/)))) stop 10
  end subroutine check_matrix

  integer function next_shift()
    evaluation_count = evaluation_count + 1
    next_shift = 1
  end function next_shift
end program nested_transform_intrinsics
