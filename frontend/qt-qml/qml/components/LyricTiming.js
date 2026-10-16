.pragma library

function activeIndex(lines, position) {
    let low = 0
    let high = lines ? lines.length : 0
    while (low < high) {
        const mid = Math.floor((low + high) / 2)
        if (Number(lines[mid].time_ms) <= position) low = mid + 1
        else high = mid
    }
    return low - 1
}

function wordIndex(words, position) {
    for (let index = 0; words && index < words.length; ++index) {
        const start = Number(words[index].time_ms)
        if (start <= position && position < start + Number(words[index].duration_ms)) return index
    }
    return -1
}

function wordProgress(word, position) {
    const start = Number(word.time_ms)
    const duration = Number(word.duration_ms)
    if (duration <= 0) return position >= start ? 1 : 0
    return Math.max(0, Math.min(1, (position - start) / duration))
}
