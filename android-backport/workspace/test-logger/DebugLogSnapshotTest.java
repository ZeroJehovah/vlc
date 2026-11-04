import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.LinkedList;
import java.util.List;
import org.videolan.vlc.util.DebugLogSnapshot;

public final class DebugLogSnapshotTest {
    private static long parcelEstimate(List<String> lines) {
        long bytes = 4;
        for (String line : lines) bytes += 8L + 2L * line.length();
        return bytes;
    }

    public static void main(String[] args) {
        assert DebugLogSnapshot.forBinder(Collections.emptyList()).isEmpty();
        List<String> small = Arrays.asList("start", "pause", "resume");
        assert DebugLogSnapshot.forBinder(small).equals(small);

        // Simulate returning from a long playback session: the previous full
        // callback could exceed Binder's 1 MiB shared transaction buffer.
        LinkedList<String> full = new LinkedList<>();
        for (int i = 0; i < 20_000; i++) full.add(i + ":" + "音频日志".repeat(1000));
        String first = full.getFirst(), last = full.getLast();
        assert parcelEstimate(full) > 1024 * 1024;
        List<String> preview = DebugLogSnapshot.forBinder(full);
        assert parcelEstimate(preview) < 70 * 1024;
        assert preview.get(preview.size() - 1).startsWith("19999:");
        assert full.size() == 20_000 && full.getFirst().equals(first) && full.getLast().equals(last);
        assert full.getLast().length() > preview.get(preview.size() - 1).length();

        // Many short lines must retain chronology and the newest entries.
        List<String> shortLines = new ArrayList<>();
        for (int i = 0; i < 1000; i++) shortLines.add(Integer.toString(i));
        preview = DebugLogSnapshot.forBinder(shortLines);
        assert preview.size() == 200;
        assert preview.get(0).equals("800") && preview.get(199).equals("999");
        assert DebugLogSnapshot.forBinder(Collections.nCopies(20_000, "")).size() == 200;
        assert parcelEstimate(DebugLogSnapshot.forBinder(Collections.singletonList("x".repeat(1_000_000)))) < 70 * 1024;
        System.out.println("PASS: full playback logs exceed Binder capacity; bounded preview fits");
        System.out.println("PASS: newest chronological records retained; complete export input unmodified");
        System.out.println("PASS: empty, many short, multilingual and oversized single-line logs");
    }
}
