"""Independent host oracle for the calculator's six rows of four buttons."""
def button_rect(width,height,top,button):
    cell_w=(width-32)//4;cell_h=(height-top-132)//6
    return 16+(button%4)*cell_w,top+116+(button//4)*cell_h,cell_w-6,cell_h-6
