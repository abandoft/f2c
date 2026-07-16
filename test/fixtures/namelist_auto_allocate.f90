module namelist_auto_allocate_types
  implicit none

  type :: leaf
    integer :: id = 0
    integer, allocatable :: values(:)
  end type leaf

  type :: container
    type(leaf), allocatable :: items(:)
    character(len=:), allocatable :: note
    character(len=:), allocatable :: tags(:)
  end type container
end module namelist_auto_allocate_types

program namelist_auto_allocate
  use namelist_auto_allocate_types
  implicit none
  type(container) :: state
  type(leaf), allocatable :: entries(:)
  character(len=:), allocatable :: labels(:)
  character(len=:), allocatable :: title
  character(4096) :: record
  namelist /snapshot/ state, entries, labels, title

  allocate(state%items(0:1))
  allocate(state%items(0)%values(-2:-1))
  allocate(state%items(1)%values(3:5))
  allocate(character(len=5) :: state%note)
  allocate(character(len=3) :: state%tags(2:3))
  allocate(entries(-1:1))
  allocate(entries(-1)%values(7:7))
  allocate(entries(0)%values(8:9))
  allocate(entries(1)%values(10:12))
  allocate(character(len=4) :: labels(-1:0))
  allocate(character(len=6) :: title)

  state%items(0)%id = 10
  state%items(0)%values(-2) = 101
  state%items(0)%values(-1) = 102
  state%items(1)%id = 20
  state%items(1)%values(3) = 201
  state%items(1)%values(4) = 202
  state%items(1)%values(5) = 203
  state%note = 'hello'
  state%tags(2) = 'one'
  state%tags(3) = 'two'
  entries(-1)%id = 30
  entries(-1)%values(7) = 301
  entries(0)%id = 40
  entries(0)%values(8) = 401
  entries(0)%values(9) = 402
  entries(1)%id = 50
  entries(1)%values(10) = 501
  entries(1)%values(11) = 502
  entries(1)%values(12) = 503
  labels(-1) = 'left'
  labels(0) = 'mid'
  title = 'report'

  write(record, nml=snapshot)
  deallocate(state%items, state%note, state%tags, entries, labels, title)
  read(record, nml=snapshot)

  if (state%items(0)%id /= 10 .or. state%items(1)%id /= 20) stop 1
  if (state%items(0)%values(-2) /= 101 .or. state%items(0)%values(-1) /= 102) stop 2
  if (state%items(1)%values(3) /= 201 .or. state%items(1)%values(5) /= 203) stop 3
  if (state%note /= 'hello' .or. state%tags(2) /= 'one' .or. state%tags(3) /= 'two') stop 10
  if (entries(-1)%id /= 30 .or. entries(0)%id /= 40 .or. entries(1)%id /= 50) stop 4
  if (entries(-1)%values(7) /= 301 .or. entries(0)%values(9) /= 402) stop 5
  if (entries(1)%values(10) /= 501 .or. entries(1)%values(12) /= 503) stop 6
  if (labels(-1) /= 'left' .or. labels(0) /= 'mid ') stop 7
  if (title /= 'report') stop 8
end program namelist_auto_allocate
