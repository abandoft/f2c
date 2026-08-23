module namelist_polymorphic_component_types
  implicit none

  type :: base_value
    integer :: identifier = 0
  contains
    procedure :: load => base_load
    procedure :: save => base_save
    procedure :: read_formatted => base_read_formatted
    procedure :: write_formatted => base_write_formatted
    generic :: read(formatted) => read_formatted
    generic :: write(formatted) => write_formatted
  end type base_value

  type, extends(base_value) :: extended_value
    integer :: payload = 0
  contains
    procedure :: load => extended_load
    procedure :: save => extended_save
    procedure :: read_formatted => extended_read_formatted
    procedure :: write_formatted => extended_write_formatted
  end type extended_value

  type :: container
    class(base_value), pointer :: item
  contains
    procedure :: read_formatted => container_read_formatted
    procedure :: write_formatted => container_write_formatted
    generic :: read(formatted) => read_formatted
    generic :: write(formatted) => write_formatted
  end type container

contains

  subroutine base_save(dtv, unit, iostat, iomsg)
    class(base_value), intent(in) :: dtv
    integer, intent(in) :: unit
    integer, intent(out) :: iostat
    character(*), intent(inout) :: iomsg

    write(unit, *, iostat=iostat, iomsg=iomsg) dtv%identifier
  end subroutine base_save

  subroutine base_load(dtv, unit, iostat, iomsg)
    class(base_value), intent(inout) :: dtv
    integer, intent(in) :: unit
    integer, intent(out) :: iostat
    character(*), intent(inout) :: iomsg

    read(unit, *, iostat=iostat, iomsg=iomsg) dtv%identifier
    if (iostat < 0) iostat = 0
  end subroutine base_load

  subroutine extended_save(dtv, unit, iostat, iomsg)
    class(extended_value), intent(in) :: dtv
    integer, intent(in) :: unit
    integer, intent(out) :: iostat
    character(*), intent(inout) :: iomsg

    write(unit, *, iostat=iostat, iomsg=iomsg) dtv%identifier, dtv%payload
  end subroutine extended_save

  subroutine extended_load(dtv, unit, iostat, iomsg)
    class(extended_value), intent(inout) :: dtv
    integer, intent(in) :: unit
    integer, intent(out) :: iostat
    character(*), intent(inout) :: iomsg

    read(unit, *, iostat=iostat, iomsg=iomsg) dtv%identifier, dtv%payload
    if (iostat < 0) iostat = 0
  end subroutine extended_load

  subroutine base_write_formatted(dtv, unit, iotype, v_list, iostat, iomsg)
    class(base_value), intent(in) :: dtv
    integer, intent(in) :: unit
    character(*), intent(in) :: iotype
    integer, intent(in) :: v_list(:)
    integer, intent(out) :: iostat
    character(*), intent(inout) :: iomsg

    write(unit, *, iostat=iostat, iomsg=iomsg) dtv%identifier
    if (len(iotype) + size(v_list) < 0) iostat = 1
  end subroutine base_write_formatted

  subroutine base_read_formatted(dtv, unit, iotype, v_list, iostat, iomsg)
    class(base_value), intent(inout) :: dtv
    integer, intent(in) :: unit
    character(*), intent(in) :: iotype
    integer, intent(in) :: v_list(:)
    integer, intent(out) :: iostat
    character(*), intent(inout) :: iomsg

    read(unit, *, iostat=iostat, iomsg=iomsg) dtv%identifier
    if (iostat < 0) iostat = 0
    if (len(iotype) + size(v_list) < 0) iostat = 1
  end subroutine base_read_formatted

  subroutine extended_write_formatted(dtv, unit, iotype, v_list, iostat, iomsg)
    class(extended_value), intent(in) :: dtv
    integer, intent(in) :: unit
    character(*), intent(in) :: iotype
    integer, intent(in) :: v_list(:)
    integer, intent(out) :: iostat
    character(*), intent(inout) :: iomsg

    write(unit, *, iostat=iostat, iomsg=iomsg) dtv%identifier, dtv%payload
    if (len(iotype) + size(v_list) < 0) iostat = 1
  end subroutine extended_write_formatted

  subroutine extended_read_formatted(dtv, unit, iotype, v_list, iostat, iomsg)
    class(extended_value), intent(inout) :: dtv
    integer, intent(in) :: unit
    character(*), intent(in) :: iotype
    integer, intent(in) :: v_list(:)
    integer, intent(out) :: iostat
    character(*), intent(inout) :: iomsg

    read(unit, *, iostat=iostat, iomsg=iomsg) dtv%identifier, dtv%payload
    if (iostat < 0) iostat = 0
    if (len(iotype) + size(v_list) < 0) iostat = 1
  end subroutine extended_read_formatted

  subroutine container_write_formatted(dtv, unit, iotype, v_list, iostat, iomsg)
    class(container), intent(in) :: dtv
    integer, intent(in) :: unit
    character(*), intent(in) :: iotype
    integer, intent(in) :: v_list(:)
    integer, intent(out) :: iostat
    character(*), intent(inout) :: iomsg

    call dtv%item%save(unit, iostat, iomsg)
    if (len(iotype) + size(v_list) < 0) iostat = 1
  end subroutine container_write_formatted

  subroutine container_read_formatted(dtv, unit, iotype, v_list, iostat, iomsg)
    class(container), intent(inout) :: dtv
    integer, intent(in) :: unit
    character(*), intent(in) :: iotype
    integer, intent(in) :: v_list(:)
    integer, intent(out) :: iostat
    character(*), intent(inout) :: iomsg

    call dtv%item%load(unit, iostat, iomsg)
    if (len(iotype) + size(v_list) < 0) iostat = 1
  end subroutine container_read_formatted
end module namelist_polymorphic_component_types

program namelist_polymorphic_component
  use namelist_polymorphic_component_types
  implicit none

  type(extended_value), target :: value
  type(container) :: box
  class(base_value), pointer :: item
  integer :: status
  character(512) :: record
  namelist /snapshot/ box
  namelist /direct_snapshot/ item

  box%item => value
  item => value
  value%identifier = 17
  value%payload = 29
  write(record, nml=snapshot, iostat=status)
  if (status /= 0) stop 1
  if (index(record, '17') == 0 .or. index(record, '29') == 0) stop 8

  value%identifier = 0
  value%payload = 0
  record = '&snapshot box=17,29 /'
  read(record, nml=snapshot, iostat=status)
  if (status > 0) stop 2
  if (value%identifier /= 17 .or. value%payload /= 29) stop 3
  if (.not. associated(box%item, value)) stop 4

  record = '&snapshot box=41,bad /'
  read(record, nml=snapshot, iostat=status)
  if (status == 0) stop 5
  if (value%identifier /= 17 .or. value%payload /= 29) stop 6
  if (.not. associated(box%item, value)) stop 7

  record = '&direct_snapshot item=53,61 /'
  read(record, nml=direct_snapshot, iostat=status)
  if (status > 0) stop 9
  if (value%identifier /= 53 .or. value%payload /= 61) stop 10
  if (.not. associated(item, value)) stop 11

  record = '&direct_snapshot item=71,bad /'
  read(record, nml=direct_snapshot, iostat=status)
  if (status == 0) stop 12
  if (value%identifier /= 53 .or. value%payload /= 61) stop 13
  if (.not. associated(item, value)) stop 14

  write(*, '(I0,1X,I0)') value%identifier, value%payload
end program namelist_polymorphic_component
