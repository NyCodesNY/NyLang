function void Main() {
    init x = 100;
    init s = "Type inference!";
    init b = true;
    
    if (b) {
        System.Print(s);
        System.Print(x);
    }
}
