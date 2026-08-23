module namelist_dynamic_shape_types
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
end module namelist_dynamic_shape_types

program namelist_dynamic_shape
  use namelist_dynamic_shape_types
  implicit none

  type(container) :: state
  type(leaf), allocatable :: entries(:)
  character(len=:), allocatable :: labels(:)
  character(len=:), allocatable :: title
  integer :: status
  character(2048) :: record
  namelist /snapshot/ state, entries, labels, title

  record = '&snapshot state%items(0:1)%id=10,20, ' // &
           'state%items(0)%values(-2:-1)=101,102, ' // &
           'state%items(1)%values(3:5)=201,202,203, ' // &
           "state%note='hello', state%tags(2:3)='one','two', " // &
           'entries(-1:1)%id=30,40,50, entries(-1)%values(7:7)=301, ' // &
           'entries(0)%values(8:9)=401,402, entries(1)%values(10:12)=501,502,503, ' // &
           "labels(-1:0)='left','mid', title='report' /"
  read(record, nml=snapshot, iostat=status)

  if (status /= 0) stop 1
  if (lbound(state%items, 1) /= 0 .or. ubound(state%items, 1) /= 1) stop 2
  if (state%items(0)%id /= 10 .or. state%items(1)%id /= 20) stop 3
  if (lbound(state%items(0)%values, 1) /= -2 .or. &
      ubound(state%items(0)%values, 1) /= -1) stop 4
  if (any(state%items(0)%values /= [101, 102])) stop 5
  if (lbound(state%items(1)%values, 1) /= 3 .or. &
      ubound(state%items(1)%values, 1) /= 5) stop 6
  if (any(state%items(1)%values /= [201, 202, 203])) stop 7
  if (state%note /= 'hello') stop 8
  if (lbound(state%tags, 1) /= 2 .or. ubound(state%tags, 1) /= 3) stop 9
  if (len(state%tags) /= 3 .or. state%tags(2) /= 'one' .or. state%tags(3) /= 'two') stop 10
  if (lbound(entries, 1) /= -1 .or. ubound(entries, 1) /= 1) stop 11
  if (entries(-1)%id /= 30 .or. entries(0)%id /= 40 .or. entries(1)%id /= 50) stop 12
  if (entries(-1)%values(7) /= 301 .or. entries(0)%values(8) /= 401 .or. &
      entries(0)%values(9) /= 402) stop 13
  if (any(entries(1)%values /= [501, 502, 503])) stop 14
  if (lbound(labels, 1) /= -1 .or. ubound(labels, 1) /= 0) stop 15
  if (len(labels) /= 4 .or. labels(-1) /= 'left' .or. labels(0) /= 'mid ') stop 16
  if (title /= 'report') stop 17
end program namelist_dynamic_shape
