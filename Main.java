import java.util.*;

public class Main {
    public static void main(String[] args) {
        Scanner in = new Scanner(System.in);
        int n = in.nextInt();
        int k = in.nextInt();
        int[]p = new int[n];
        int[]copy = new int[n];
        for (int i = 0; i < n; i++) {
            p[i] = in.nextInt();
            copy[i] = p[i];
        }
        Arrays.sort(copy);
        int maxVal = 0;
        for (int i = 0; i < k; i++) {
            maxVal += (copy[n-1-i] % 998244353);
        }
        System.out.print((maxVal % 998244353) + " ");
        int []pos = new int[k];
        for (int i = 0; i < n; i++) {
            // 增加具体算法
        }
    }
}
