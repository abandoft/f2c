program namelist_derived_sections
  implicit none

  type :: leaf
    integer :: id
    integer, allocatable :: values(:)
  end type leaf

  type :: pair
    integer :: key
    integer :: value
  end type pair

  type :: box
    type(leaf), allocatable :: items(:)
    type(leaf), pointer :: link
  end type box

  type(box) :: state
  type(leaf), target :: linked
  type(pair) :: entries(2)
  type(pair), allocatable :: dynamic_entries(:)
  integer :: status
  character(1024) :: record
  namelist /snapshot/ state, entries, dynamic_entries

  allocate(state%items(2))
  allocate(state%items(1)%values(2), state%items(2)%values(3))
  allocate(linked%values(2), dynamic_entries(2))
  state%link => linked
  state%items(1)%id = 1
  state%items(1)%values = [2, 3]
  state%items(2)%id = 4
  state%items(2)%values = [5, 6, 7]
  linked%id = 8
  linked%values = [9, 10]
  entries(1)%key = 11
  entries(1)%value = 12
  entries(2)%key = 13
  entries(2)%value = 14
  dynamic_entries(1)%key = 15
  dynamic_entries(1)%value = 16
  dynamic_entries(2)%key = 17
  dynamic_entries(2)%value = 18

  record = '&snapshot entries=101,102,103,104, dynamic_entries=105,106,107,108, ' // &
           'state%items(::-1)%id=204,201, state%items(1)%values(::-1)=203,202, ' // &
           'state%items(2)%values(::-1)=207,206,205, state%link%id=208, ' // &
           'state%link%values(::-1)=210,209 /'
  read(record, nml=snapshot, iostat=status)

  if (status /= 0) stop 1
  if (entries(1)%key /= 101 .or. entries(1)%value /= 102) stop 2
  if (entries(2)%key /= 103 .or. entries(2)%value /= 104) stop 3
  if (dynamic_entries(1)%key /= 105 .or. dynamic_entries(1)%value /= 106) stop 4
  if (dynamic_entries(2)%key /= 107 .or. dynamic_entries(2)%value /= 108) stop 5
  if (state%items(1)%id /= 201 .or. any(state%items(1)%values /= [202, 203])) stop 6
  if (state%items(2)%id /= 204 .or. any(state%items(2)%values /= [205, 206, 207])) stop 7
  if (linked%id /= 208 .or. any(linked%values /= [209, 210])) stop 8

  record = '&snapshot state%items(::-1)%id=1,2, entries(99)%key=3 /'
  read(record, nml=snapshot, iostat=status)
  if (status == 0) stop 9
  if (state%items(1)%id /= 201 .or. state%items(2)%id /= 204) stop 10
  if (entries(1)%key /= 101 .or. entries(2)%key /= 103) stop 11
end program namelist_derived_sections
