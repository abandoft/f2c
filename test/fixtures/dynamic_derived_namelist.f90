module dynamic_namelist_types
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
  contains
    procedure :: read_formatted => box_read_formatted
    procedure :: write_formatted => box_write_formatted
    generic :: read(formatted) => read_formatted
    generic :: write(formatted) => write_formatted
  end type box

contains

  subroutine box_write_formatted(dtv, unit, iotype, v_list, iostat, iomsg)
    class(box), intent(in) :: dtv
    integer, intent(in) :: unit
    character(*), intent(in) :: iotype
    integer, intent(in) :: v_list(:)
    integer, intent(out) :: iostat
    character(*), intent(inout) :: iomsg

    if (iotype(1:2) == 'DT') then
      write(unit, *, iostat=iostat, iomsg=iomsg) v_list(1)
      return
    end if
    write(unit, *, iostat=iostat, iomsg=iomsg) &
      dtv%items(1)%id, dtv%items(1)%values(1), dtv%items(1)%values(2), &
      dtv%items(2)%id, dtv%items(2)%values(1), dtv%items(2)%values(2), &
      dtv%items(2)%values(3), dtv%link%id, dtv%link%values(1), dtv%link%values(2)
  end subroutine box_write_formatted

  subroutine box_read_formatted(dtv, unit, iotype, v_list, iostat, iomsg)
    class(box), intent(inout) :: dtv
    integer, intent(in) :: unit
    character(*), intent(in) :: iotype
    integer, intent(in) :: v_list(:)
    integer, intent(out) :: iostat
    character(*), intent(inout) :: iomsg

    if (iotype(1:2) == 'DT') then
      read(unit, *, iostat=iostat, iomsg=iomsg) dtv%items(1)%id
      if (v_list(1) == -1) dtv%items(1)%id = 0
      return
    end if
    read(unit, *, iostat=iostat, iomsg=iomsg) &
      dtv%items(1)%id, dtv%items(1)%values(1), dtv%items(1)%values(2), &
      dtv%items(2)%id, dtv%items(2)%values(1), dtv%items(2)%values(2), &
      dtv%items(2)%values(3), dtv%link%id, dtv%link%values(1), dtv%link%values(2)
  end subroutine box_read_formatted
end module dynamic_namelist_types

program dynamic_derived_namelist
  use dynamic_namelist_types
  implicit none
  type(box) :: state
  type(leaf), target :: linked
  type(pair) :: entries(2)
  type(pair), allocatable :: dynamic_entries(:)
  character(2048) :: record
  namelist /snapshot/ state, entries, dynamic_entries

  allocate(state%items(2))
  allocate(state%items(1)%values(2))
  allocate(state%items(2)%values(3))
  allocate(linked%values(2))
  allocate(dynamic_entries(2))
  state%link => linked

  state%items(1)%id = 11
  state%items(1)%values(1) = 1
  state%items(1)%values(2) = 2
  state%items(2)%id = 22
  state%items(2)%values(1) = 3
  state%items(2)%values(2) = 4
  state%items(2)%values(3) = 5
  linked%id = 33
  linked%values(1) = 6
  linked%values(2) = 7
  entries(1)%key = 8
  entries(1)%value = 9
  entries(2)%key = 10
  entries(2)%value = 12
  dynamic_entries(1)%key = 14
  dynamic_entries(1)%value = 15
  dynamic_entries(2)%key = 16
  dynamic_entries(2)%value = 18
  write(record, nml=snapshot)

  state%items(1)%id = 0
  state%items(1)%values(1) = 0
  state%items(1)%values(2) = 0
  state%items(2)%id = 0
  state%items(2)%values(1) = 0
  state%items(2)%values(2) = 0
  state%items(2)%values(3) = 0
  linked%id = 0
  linked%values(1) = 0
  linked%values(2) = 0
  entries(1)%key = 0
  entries(1)%value = 0
  entries(2)%key = 0
  entries(2)%value = 0
  dynamic_entries(1)%key = 0
  dynamic_entries(1)%value = 0
  dynamic_entries(2)%key = 0
  dynamic_entries(2)%value = 0
  read(record, nml=snapshot)

  if (state%items(1)%id /= 11) stop 1
  if (state%items(1)%values(1) /= 1 .or. state%items(1)%values(2) /= 2) stop 2
  if (state%items(2)%id /= 22) stop 3
  if (state%items(2)%values(1) /= 3 .or. state%items(2)%values(2) /= 4 .or. &
      state%items(2)%values(3) /= 5) stop 4
  if (linked%id /= 33 .or. linked%values(1) /= 6 .or. linked%values(2) /= 7) stop 5
  if (entries(1)%key /= 8 .or. entries(1)%value /= 9) stop 6
  if (entries(2)%key /= 10 .or. entries(2)%value /= 12) stop 7
  if (dynamic_entries(1)%key /= 14 .or. dynamic_entries(1)%value /= 15) stop 8
  if (dynamic_entries(2)%key /= 16 .or. dynamic_entries(2)%value /= 18) stop 9
end program dynamic_derived_namelist
