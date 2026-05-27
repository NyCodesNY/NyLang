function int testTypes() {
    int x = 42;
    string s = "Static Typing Works!";
    bool flag = true;
    
    if (flag) {
        System.Print(s);
        System.Print(x);
    }
    
    return 0;
}

function void Main() {
    int ret = testTypes();
}
