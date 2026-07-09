from yetty import runtime
from yetty.ygui import window
from yetty.ygui import button



def main():
    res = runtime()
    if res.error:
        print("error initializing runtime")
        return

    rt = res.value

    res = window(rt)

    if res.error:
        print("error initializing runtime")
        return

    w = res.value

    v.add(button)

    
    

