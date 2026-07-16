program transform_character_derived
  implicit none

  type :: item
    integer :: value = 0
    integer, allocatable :: payload(:)
  end type item

  character(len=3) :: words(4), matrix(2, 2), selected(3), vector(3)
  character(len=3) :: expanded(2, 4), shifted_words(4), ended_words(4)
  character(len=:), allocatable :: dynamic_words(:)
  integer :: location(1)
  logical :: word_mask(4), item_mask(3)
  type(item) :: items(3), item_shape(3), item_selected(2), item_vector(2)
  type(item) :: item_unpacked(3), item_expanded(2, 3), item_shifted(3), item_ended(3)
  type(item), allocatable :: dynamic_items(:)
  type(item) :: boundary

  words(1) = 'aa'
  words(2) = 'b'
  words(3) = 'ccc'
  words(4) = 'dd'
  word_mask = (/.true., .false., .true., .false./)
  vector(1) = 'v1'
  vector(2) = 'v2'
  vector(3) = 'v3'

  matrix = reshape(words, (/2, 2/))
  selected = pack(words, word_mask, vector)
  shifted_words = cshift(words, 1)
  ended_words = eoshift(words, 1, boundary='end')
  expanded = spread(words, 1, 2)
  location = findloc(words, 'ccc')
  dynamic_words = pack(words, word_mask)

  if (matrix(1, 1) /= 'aa ' .or. matrix(2, 1) /= 'b  ') stop 1
  if (matrix(1, 2) /= 'ccc' .or. matrix(2, 2) /= 'dd ') stop 2
  if (selected(1) /= 'aa ' .or. selected(2) /= 'ccc' .or. selected(3) /= 'v3 ') stop 3
  if (shifted_words(1) /= 'b  ' .or. shifted_words(4) /= 'aa ') stop 4
  if (ended_words(1) /= 'b  ' .or. ended_words(4) /= 'end') stop 5
  if (expanded(1, 3) /= 'ccc' .or. expanded(2, 4) /= 'dd ') stop 6
  if (location(1) /= 3) stop 7
  if (dynamic_words(1) /= 'aa ' .or. dynamic_words(2) /= 'ccc') stop 15

  items(1)%value = 10
  items(2)%value = 20
  items(3)%value = 30
  allocate(items(1)%payload(1), items(2)%payload(1), items(3)%payload(1))
  items(1)%payload(1) = 101
  items(2)%payload(1) = 202
  items(3)%payload(1) = 303
  item_mask = (/.true., .false., .true./)
  item_vector(1) = items(2)
  item_vector(2) = items(1)
  boundary%value = -1
  allocate(boundary%payload(1))
  boundary%payload(1) = -101

  item_shape = reshape(items, (/3/))
  item_selected = pack(items, item_mask)
  item_unpacked = unpack(item_selected, item_mask, boundary)
  item_expanded = spread(items, 1, 2)
  item_shifted = cshift(items, 1)
  item_ended = eoshift(items, 1, boundary)
  dynamic_items = reshape(items, (/3/))

  items(1)%payload(1) = 999
  boundary%payload(1) = -999
  if (item_shape(1)%payload(1) /= 101 .or. item_shape(3)%value /= 30) stop 8
  if (item_selected(1)%value /= 10 .or. item_selected(2)%payload(1) /= 303) stop 9
  if (item_unpacked(1)%payload(1) /= 101 .or. item_unpacked(2)%payload(1) /= -101) stop 10
  if (item_expanded(2, 2)%payload(1) /= 202) stop 11
  if (item_shifted(1)%value /= 20 .or. item_shifted(3)%payload(1) /= 101) stop 12
  if (item_ended(1)%value /= 20 .or. item_ended(3)%payload(1) /= -101) stop 13
  if (dynamic_items(1)%payload(1) /= 101 .or. dynamic_items(3)%value /= 30) stop 17
end program transform_character_derived
